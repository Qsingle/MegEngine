/**
 * \file dnn/src/cuda/conv_bias/implicit_gemm_int8_nchw32_imma.cpp
 * MegEngine is Licensed under the Apache License, Version 2.0 (the "License")
 *
 * Copyright (c) 2014-2020 Megvii Inc. All rights reserved.
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT ARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 */

#include "./algo.h"
#include "src/cuda/conv_bias/cutlass_convolution_wrapper.cuh"
#include "src/cuda/convolution_helper/parameter.cuh"
#include "src/cuda/utils.h"

using namespace megdnn;
using namespace cuda;
using namespace convolution;

#if CUDA_VERSION >= 10020
bool ConvBiasForwardImpl::AlgoInt8NCHW32IMMAImplicitGemm::is_available(
        const SizeArgs& args) const {
    if (args.bias_layout->ndim <= 0)
        return false;

    using Param = param::ConvBias;
    using Format = Param::Format;
    using Sparse = Param::Sparse;
    using Mode = Param::Mode;
    bool available = true;
    auto&& param = args.opr->param();
    auto&& fm = args.filter_meta;
    if (!conv_bias::check_bias_share_in_channel(*(args.bias_layout),
                                                param.format))
        return false;
    if (param.format != Format::NCHW32)
        return false;
    UNPACK_CONV_BIAS_NCHW32_PARAM(*(args.src_layout), fm, *(args.dst_layout),
                                  param);
    // TODO support group conv
    available &= param.sparse == Sparse::DENSE;
    // mode must be cross correlation
    available &= param.mode == Mode::CROSS_CORRELATION;
    // check data type
    auto src_dtype = args.src_layout->dtype,
         filter_dtype = args.filter_layout->dtype,
         bias_dtype = args.bias_layout->dtype,
         dst_dtype = args.dst_layout->dtype;
    available &= (src_dtype.enumv() == DTypeEnum::QuantizedS8 &&
                  filter_dtype.enumv() == DTypeEnum::QuantizedS8 &&
                  bias_dtype.enumv() == DTypeEnum::QuantizedS32 &&
                  dst_dtype.enumv() == DTypeEnum::QuantizedS8);
    // TODO: support dialtion
    available &= dh == 1 && dw == 1;
    // only support sm_75 or later, platform should have tensorcore int8
    // support
    available &= is_compute_capability_required(7, 5);
    if (fh == 1 && fw == 1)
        return available;
    // for non 1x1 convolution, we have to check constant memory size
    auto&& device_prop = current_device_prop();
    // const mem size >= 64K
    available &= device_prop.totalConstMem >= 65536;
    size_t const_mem_usage = get_workspace_in_bytes(args) -
                             args.filter_layout->span().dist_byte();
    available &= const_mem_usage <= device_prop.totalConstMem;
    return available;
}

WorkspaceBundle
ConvBiasForwardImpl::AlgoInt8NCHW32IMMAImplicitGemm::get_workspace_bundle(
        dt_byte* raw_ptr, const SizeArgs& args) const {
    size_t ci = args.filter_layout->operator[](1) * 32;
    size_t fh = args.filter_layout->operator[](2);
    size_t fw = args.filter_layout->operator[](3);
    size_t ws_filter = args.filter_layout->span().dist_byte();
    if (fh == 1 && fw == 1) {
        return WorkspaceBundle{raw_ptr, {ws_filter}};
    }
    size_t ws_size = (ci / 32) * fh * fw * sizeof(int32_t) * 2;
    return WorkspaceBundle{raw_ptr, {ws_filter, ws_size}};
}

size_t
ConvBiasForwardImpl::AlgoInt8NCHW32IMMAImplicitGemm::get_workspace_in_bytes(
        const SizeArgs& args) const {
    return get_workspace_bundle(nullptr, args).total_size_in_bytes();
}

void ConvBiasForwardImpl::AlgoInt8NCHW32IMMAImplicitGemm::exec(
        const ExecArgs& args) const {
    using Format = Param::Format;
    auto&& param = args.opr->param();
    auto&& fm = args.filter_meta;
    UNPACK_CONV_BIAS_NCHW32_PARAM(*(args.src_layout), fm, *(args.dst_layout),
                                  param);
    auto ws = get_workspace_bundle(args.workspace.raw_ptr, args);
    auto ws_filter = ws.get(0);
    auto&& stream = cuda_stream(args.opr->handle());

    // reformat filter from nchw32 to chwn32
    {
        TensorLayout src{{co, ci / 32, fh, fw, 32}, dtype::Int8()};
        src.init_contiguous_stride();
        TensorLayout dst = src;
        dst.stride[0] = 32;
        dst.stride[1] = co * fh * fw * 32;
        dst.stride[2] = co * fw * 32;
        dst.stride[3] = co * 32;
        dst.stride[4] = 1;
        TensorND ts_src, ts_dst;
        ts_src.raw_ptr = args.filter_tensor->raw_ptr;
        ts_src.layout = src;
        ts_dst.raw_ptr = ws_filter;
        ts_dst.layout = dst;
        auto&& transpose =
                args.opr->handle()->create_operator<RelayoutForward>();
        transpose->exec(ts_src, ts_dst);
    }

    ConvParam kern_param;
    kern_param.n = n, kern_param.co = co, kern_param.ci = ci,
    kern_param.hi = hi, kern_param.wi = wi, kern_param.ho = ho,
    kern_param.wo = wo, kern_param.ph = ph, kern_param.pw = pw,
    kern_param.sh = sh, kern_param.sw = sw, kern_param.fh = fh,
    kern_param.fw = fw;

    float src_scale = args.src_layout->dtype.param<dtype::QuantizedS8>().scale,
          filter_scale =
                  args.filter_layout->dtype.param<dtype::QuantizedS8>().scale,
          bias_scale =
                  args.bias_layout->dtype.param<dtype::QuantizedS32>().scale,
          dst_scale = args.dst_layout->dtype.param<dtype::QuantizedS8>().scale;
    float alpha = src_scale * filter_scale / dst_scale,
          beta = bias_scale / dst_scale;
    int8_t* z_dev_ptr = nullptr;
    float gamma = 0.0;
    if (args.z_layout->ndim > 0) {
        z_dev_ptr = args.z_tensor->compatible_ptr<int8_t>();
        float z_scale = args.z_layout->dtype.param<dtype::QuantizedS8>().scale;
        gamma = z_scale / dst_scale;
    }
    uint32_t nonlinear_mode = static_cast<uint32_t>(param.nonlineMode);
    if (fh == 1 && fw == 1) {
        cutlass_wrapper::do_conv_bias_int8_implicit_gemm_imma_ncdiv32hw32<
                false>(args.src_tensor->compatible_ptr<int8_t>(),
                       reinterpret_cast<int8_t*>(ws_filter),
                       args.bias_tensor->compatible_ptr<int32_t>(), z_dev_ptr,
                       args.dst_tensor->compatible_ptr<int8_t>(),
                       nullptr, kern_param, nonlinear_mode,
                       alpha, beta, gamma, dst_scale,
                       cutlass_wrapper::GemmCoord{m_algo_param.threadblock_m,
                                                  m_algo_param.threadblock_n,
                                                  m_algo_param.threadblock_k},
                       cutlass_wrapper::GemmCoord{m_algo_param.warp_m,
                                                  m_algo_param.warp_n,
                                                  m_algo_param.warp_k},
                       stream);
    } else {
        auto workspace = ws.get(1);
        cutlass_wrapper::do_conv_bias_int8_implicit_gemm_imma_ncdiv32hw32<true>(
                args.src_tensor->compatible_ptr<int8_t>(),
                reinterpret_cast<int8_t*>(ws_filter),
                args.bias_tensor->compatible_ptr<int32_t>(), z_dev_ptr,
                args.dst_tensor->compatible_ptr<int8_t>(),
                reinterpret_cast<int*>(workspace), kern_param, nonlinear_mode,
                alpha, beta, gamma, dst_scale,
                cutlass_wrapper::GemmCoord{m_algo_param.threadblock_m,
                                           m_algo_param.threadblock_n,
                                           m_algo_param.threadblock_k},
                cutlass_wrapper::GemmCoord{m_algo_param.warp_m,
                                           m_algo_param.warp_n,
                                           m_algo_param.warp_k},
                stream);
    }
}

std::string ConvBiasForwardImpl::AlgoInt8NCHW32IMMAImplicitGemm::to_string(
        AlgoParam algo_param) {
    return ssprintf("%uX%uX%u_%uX%uX%u", algo_param.threadblock_m,
                    algo_param.threadblock_n, algo_param.threadblock_k,
                    algo_param.warp_m, algo_param.warp_n, algo_param.warp_k);
}
#endif

// vim: syntax=cpp.doxygen
