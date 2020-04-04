; target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
%Render_Target = type { i32 }
%combined_image_t = type { i32 }
%image_t = type { i32 }
%sampler_t = type { i32 }

declare void @spv_dummy_sampler_t_use(%sampler_t )
declare void @spv_dummy_combined_image_t_use(%combined_image_t )

; declare void @store_rt_2d_f4(%Render_Target* %0, <2 x i32> %coord, <4 x float> %val)
; separate sampler+image sample function
; declare <4 x float> @sample_sepr_2d_f4(%sampler_t* %smp, %image_t *%img, <2 x float> %uv)
; combined sampler function
; declare <4 x float> @sample_comb_2d_f4(%combined_image_t %cimg, <2 x float> %uv)
declare void @spv_image_read_f4(%image_t *%cimg, <2 x i32> *%in_coord, <4 x float> *%out_data)
declare void @spv_image_write_f4(%image_t *%cimg, <2 x i32> *%in_coord, <4 x float> *%in_data)

declare i8 addrspace(9) *@get_push_constant_ptr()
declare i8 addrspace(2) *@get_uniform_ptr(i32 %set, i32 %binding)
declare i8 addrspace(0) *@get_uniform_const_ptr(i32 %set, i32 %binding)
declare i8 addrspace(12) *@get_storage_ptr(i32 %set, i32 %binding)
declare i8 addrspace(1) *@get_input_ptr(i32 %id)
declare i8 addrspace(3) *@get_output_ptr(i32 %id)
declare i8 addrspace(6) *@get_private_ptr()

declare void @kill()
declare <4 x float> @dummy_sample()

declare <2 x float> @normalize_f2(<2 x float> *%in)
declare <3 x float> @normalize_f3(<3 x float> *%in)
declare <4 x float> @normalize_f4(<4 x float> *%in)

declare float @spv_dot_f2(<2 x float> *%a, <2 x float> *%b)
declare float @spv_dot_f3(<3 x float> *%a, <3 x float> *%b)
declare float @spv_dot_f4(<4 x float> *%a, <4 x float> *%b)

declare float @length_f2(<2 x float> *%in)
declare float @length_f3(<3 x float> *%in)
declare float @length_f4(<4 x float> *%in)

declare void @spv_get_global_invocation_id(<3 x i32> *%out)
declare void @spv_get_work_group_size(<3 x i32> *%out)

declare float @spv_sqrt(float %in)

declare void @spv_on_exit()
