#if !MEGDNN_TEGRA_X1
// generated by gen_cuda_conv_bias_kern_impls.py
// ignore warning of cutlass
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#include "../conv_bias_int8_implicit_gemm_imma_ncdiv32hw32.cuinl"

using LayoutSrc = cutlass::layout::TensorNCxHWx<32>;
using LayoutFilter = cutlass::layout::TensorCxRSKx<32>;
using ThreadBlockShape = cutlass::gemm::GemmShape<128, 64, 64>;
using WarpShape = cutlass::gemm::GemmShape<64, 32, 64>;
using InstructionShape = cutlass::gemm::GemmShape<8, 8, 16>;
using EpilogueOp = cutlass::epilogue::thread::BiasAddLinearCombinationClamp<
                    int8_t, 8, int32_t, int32_t, float>;
using Convolution = cutlass::convolution::device::Convolution<
    int8_t, LayoutSrc, int8_t, LayoutFilter, int8_t, 
    LayoutSrc, int32_t, LayoutSrc, int32_t, 
    cutlass::convolution::ConvType::kConvolution, cutlass::arch::OpClassTensorOp, cutlass::arch::Sm75, 
    ThreadBlockShape, WarpShape, InstructionShape, EpilogueOp, 
    cutlass::convolution::threadblock::ConvolutionNCxHWxThreadblockSwizzle<
    cutlass::convolution::ConvType::kConvolution>, 
    2, 16, 16, false>;
template void megdnn::cuda::cutlass_wrapper::cutlass_convolution_wrapper<Convolution>(
        const int8_t* d_src, 
        const int8_t* d_filter, 
        const int32_t* d_bias, 
        const int8_t* d_z, 
        int8_t* d_dst, 
        int* workspace, 
        typename Convolution::ConvolutionParameter const& conv_param, 
        typename Convolution::EpilogueOutputOp::Params const& epilogue, 
        cudaStream_t stream);
#pragma GCC diagnostic pop
#endif
