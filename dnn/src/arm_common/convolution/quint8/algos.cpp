/**
 * \file dnn/src/arm_common/convolution/quint8/algos.cpp
 * MegEngine is Licensed under the Apache License, Version 2.0 (the "License")
 *
 * Copyright (c) 2014-2020 Megvii Inc. All rights reserved.
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT ARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */

#include "src/arm_common/convolution/img2col_helper.h"
#include "src/arm_common/convolution/quint8/algos.h"
#include "src/arm_common/convolution/quint8/conv_backdata_stride1.h"
#include "src/arm_common/convolution/quint8/conv_backdata_stride2.h"
#include "src/common/opr_delegate.h"
#include "midout.h"

MIDOUT_DECL(megdnn_arm_conv_quint8_kimpl)

using namespace megdnn;
using namespace arm_common;

#if __ARM_FEATURE_DOTPROD

/* ===================== ConvolutionBackwardData  ===================== */

/* ===================== direct stride 1 algo ===================== */
bool ConvolutionBackwardDataImpl::AlgoUdot8DirectStride1::usable(
        fallback::ConvolutionBackwardDataImpl*,
        const NCBKernSizeParam& param) const {
    return deconv::can_stride1_quint8_dot(param);
}

size_t ConvolutionBackwardDataImpl::AlgoUdot8DirectStride1::get_workspace(
        fallback::ConvolutionBackwardDataImpl*,
        const NCBKernSizeParam& param) const {
    MIDOUT_BEGIN(megdnn_arm_conv_quint8_kimpl,
                 midout_iv("AlgoUdot8DirectStride1::get_workspace"_hash)) {
        return deconv::get_workspace_in_bytes_stride1_quint8_dot(param);
    }
    MIDOUT_END();
    return 0;
}

ConvolutionBackwardDataImpl::ncb_kern_t
ConvolutionBackwardDataImpl::AlgoUdot8DirectStride1::dispatch_kern(
        fallback::ConvolutionBackwardDataImpl*, const NCBKernSizeParam&) const {
    MIDOUT_BEGIN(megdnn_arm_conv_quint8_kimpl,
                 midout_iv("AlgoUdot8DirectStride1::dispatch_kern"_hash)) {
        return deconv::stride1_quint8_dot;
    }
    MIDOUT_END();
    return {};
}

/* ===================== direct stride 2 algo ===================== */
bool ConvolutionBackwardDataImpl::AlgoUdot8DirectStride2::usable(
        fallback::ConvolutionBackwardDataImpl*,
        const NCBKernSizeParam& param) const {
    return deconv::can_stride2_quint8_dot(param);
}

size_t ConvolutionBackwardDataImpl::AlgoUdot8DirectStride2::get_workspace(
        fallback::ConvolutionBackwardDataImpl*,
        const NCBKernSizeParam& param) const {
    MIDOUT_BEGIN(megdnn_arm_conv_quint8_kimpl,
                 midout_iv("AlgoUdot8DirectStride2::get_workspace"_hash)) {
        return deconv::get_workspace_in_bytes_stride2_quint8_dot(param);
    }
    MIDOUT_END();
    return 0;
}

ConvolutionBackwardDataImpl::ncb_kern_t
ConvolutionBackwardDataImpl::AlgoUdot8DirectStride2::dispatch_kern(
        fallback::ConvolutionBackwardDataImpl*, const NCBKernSizeParam&) const {
    MIDOUT_BEGIN(megdnn_arm_conv_quint8_kimpl,
                 midout_iv("AlgoUdot8DirectStride2::dispatch_kern"_hash)) {
        return deconv::stride2_quint8_dot;
    }
    MIDOUT_END();
    return {};
}
#endif
// vim: syntax=cpp.doxygen
