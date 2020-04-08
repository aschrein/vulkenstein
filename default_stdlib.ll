; target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
%Render_Target = type { i64 }
%combined_image_t = type { i64 }
%image_t = type { i64 }
%sampler_t = type { i64 }
%mask_t = type i64
%state_t = type i8

declare void @spv_dummy_sampler_t_use(%sampler_t )
declare void @spv_dummy_combined_image_t_use(%combined_image_t )

; declare void @store_rt_2d_f4(%Render_Target* %0, <2 x i32> %coord, <4 x float> %val)
; separate sampler+image sample function
; declare <4 x float> @sample_sepr_2d_f4(%sampler_t* %smp, %image_t *%img, <2 x float> %uv)
; combined sampler function
; declare <4 x float> @sample_comb_2d_f4(%combined_image_t %cimg, <2 x float> %uv)
declare <4 x float> @spv_image_read_f4(%image_t %cimg, <2 x i32> %in_coord)
declare void @spv_image_write_f4(%image_t %cimg, <2 x i32> %in_coord, <4 x float> %in_data)

declare align 16 i8 *@get_push_constant_ptr(%state_t *) #0
declare align 16 i8 *@get_uniform_ptr(%state_t *, i32 %set, i32 %binding) #0
declare align 16 i8 *@get_uniform_const_ptr(%state_t *, i32 %set, i32 %binding) #0
declare align 16 i8 *@get_storage_ptr(%state_t *, i32 %set, i32 %binding) #0
declare align 16 i8 *@get_input_ptr(%state_t *) #0
declare align 16 i8 *@get_output_ptr(%state_t *) #0
declare align 16 i8 *@get_private_ptr(%state_t *) #0

declare void @kill(%state_t *, %mask_t %mask)
declare <4 x float> @dummy_sample()

declare <2 x float> @normalize_f2(<2 x float> %in) #0
declare <3 x float> @normalize_f3(<3 x float> %in) #0
declare <4 x float> @normalize_f4(<4 x float> %in) #0

declare float @spv_dot_f2(<2 x float> %a, <2 x float> %b) #0
declare float @spv_dot_f3(<3 x float> %a, <3 x float> %b) #0
declare float @spv_dot_f4(<4 x float> %a, <4 x float> %b) #0

declare <3 x float> @spv_cross(<3 x float> %a, <3 x float> %b) #0

declare float @spv_length_f2(<2 x float> %in) #0
declare float @spv_length_f3(<3 x float> %in) #0
declare float @spv_length_f4(<4 x float> %in) #0

declare <3 x i32> @spv_get_global_invocation_id(%state_t *, i32 %lane_id) #0
declare <3 x i32> @spv_get_work_group_size(%state_t *) #0

declare float @spv_sqrt(float %in) #0

attributes #0 = { nounwind readnone speculatable }
