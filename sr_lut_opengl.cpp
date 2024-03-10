#include "sr_lut_opengl.h"
#include "lut_data.h"
#include "rtc_base/logging.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "third_party/libyuv/include/libyuv/planar_functions.h"
#include "api/video/video_frame_buffer.h"
#include "rtc_base/timeutils.h"
#include <math.h>
#include "sdk/android/src/jni/videoframe.h"
#include "sdk/android/src/jni/wrapped_native_i420_buffer.h"

#define STRINGIZE(x)  #x
#define SHADER_STRING(text) STRINGIZE(text)

namespace webrtc {

	static const GLfloat RectangleBuffer[] = {
		-1.0f, -1.0f, // Bottom left.
		1.0f, -1.0f, // Bottom right.
		-1.0f, 1.0f, // Top left.
		1.0f, 1.0f, // Top right.
	};
	static const GLfloat RectangleTextureBuffer[] = {
		0.0f, 0.0f, // Bottom left.
		1.0f, 0.0f, // Bottom right.
		0.0f, 1.0f, // Top left.
		1.0f, 1.0f, // Top right.
	};

	SRLutOpenGL::SRLutOpenGL(VideoFilterType filter_type) : BaseSuperResolution(filter_type) {
		hardware_type_ = VideoHardwareType::kGPUType;

		mInit = false;
		//mInitLut = false;
		src_width = 0;
		src_height = 0;
		dst_width = 0;
		dst_height = 0;
		tex_width = 0;
		tex_height = 0;

		mOpenGLContext = NULL;
		DefineShaders();
		RTC_LOG(LS_INFO) << "[Process] create SRLutOpenGL";
	}

	SRLutOpenGL::~SRLutOpenGL() {
		RTC_LOG(LS_INFO) << "[Process] release SRLutOpenGL";
	}

	bool SRLutOpenGL::isNeedChangeFilter(VideoFrame& input_frame){
		return false;
	}

	void SRLutOpenGL::DefineShaders() {
		kLutVertexShaderString = SHADER_STRING
		(
			attribute vec4 position;
			attribute vec4 texCoord;
			varying vec2 textureCoordinate;

		void main()
		{
			gl_Position = position;
			textureCoordinate = texCoord.xy;
		}
		);

		kLutOptFragmentShaderString = SHADER_STRING
		(
		precision mediump float;
		varying highp vec2 textureCoordinate;

		uniform sampler2D inputImageTexture;
		uniform sampler2D inputLUTTexture;
		uniform float srcSize_w;
		uniform float srcSize_h;
		uniform float dstSize_w;
		uniform float dstSize_h;
		uniform float step_lut;

		void main(){
    		const float err = 128.0/255.0;
    		const float L = 17.0;
    
    		vec2 cur_pos_dst_XY = floor(vec2(textureCoordinate.x * dstSize_w, textureCoordinate.y * dstSize_h));//0-512
    		vec2 cur_pos_src_XY = floor(cur_pos_dst_XY / 2.0);  //0-256

    		int pixel_case = -1;
    		mat4 indexMat4 = mat4(0.0, 1.0, 3.0, 2.0, //in[0]
                        		  1.0, 3.0, 2.0, 0.0,
                        		  2.0, 0.0, 1.0, 3.0,
                        		  3.0, 2.0, 0.0, 1.0);
    
    		vec2 pix_case_helper = ceil(cur_pos_dst_XY/ 2.0 - floor(cur_pos_dst_XY / 2.0));
    		pixel_case = int(pix_case_helper.x * 1.0 + pix_case_helper.y * 2.0);

    		vec4 indexVec4 = indexMat4[pixel_case];
    
    		float color_a = texture2D(inputImageTexture, vec2((cur_pos_src_XY.x -0.5)/srcSize_w, (cur_pos_src_XY.y -0.5)/srcSize_h)).r;
    		float color_b = texture2D(inputImageTexture, vec2((cur_pos_src_XY.x +0.5)/srcSize_w, (cur_pos_src_XY.y -0.5)/srcSize_h)).r;
    		float color_c = texture2D(inputImageTexture, vec2((cur_pos_src_XY.x +1.5)/srcSize_w, (cur_pos_src_XY.y -0.5)/srcSize_h)).r;
    		float color_d = texture2D(inputImageTexture, vec2((cur_pos_src_XY.x -0.5)/srcSize_w, (cur_pos_src_XY.y +0.5)/srcSize_h)).r;
    		float color_e = texture2D(inputImageTexture, vec2((cur_pos_src_XY.x +0.5)/srcSize_w, (cur_pos_src_XY.y +0.5)/srcSize_h)).r;
    		float color_f = texture2D(inputImageTexture, vec2((cur_pos_src_XY.x +1.5)/srcSize_w, (cur_pos_src_XY.y +0.5)/srcSize_h)).r;
    		float color_g = texture2D(inputImageTexture, vec2((cur_pos_src_XY.x -0.5)/srcSize_w, (cur_pos_src_XY.y +1.5)/srcSize_h)).r;
    		float color_h = texture2D(inputImageTexture, vec2((cur_pos_src_XY.x +0.5)/srcSize_w, (cur_pos_src_XY.y +1.5)/srcSize_h)).r;
    		float color_i = texture2D(inputImageTexture, vec2((cur_pos_src_XY.x +1.5)/srcSize_w, (cur_pos_src_XY.y +1.5)/srcSize_h)).r;

			//4s5r
    		color_e = floor(color_e * 255.0 + 0.5);
    		float img_e = floor(color_e / 16.0);
    		float f_e = color_e - 16.0 * img_e;
    
    		vec4 color_abcd = floor(vec4(color_a, color_b, color_c, color_d)* 255.0 + 0.5);  //0-255
    		vec4 img_abcd = floor(color_abcd /16.0);  //0-15, 4pixels
    		vec4 f_abcd = color_abcd - 16.0*img_abcd; //0-15, 4pixels

    		vec4 color_fghi = floor(vec4(color_f, color_g, color_h, color_i) * 255.0 + 0.5);  //0-255
    		vec4 img_fghi = floor(color_fghi /16.0);  //0-15, 4pixels
    		vec4 f_fghi = color_fghi - 16.0*img_fghi; //0-15, 4pixels
    
    		vec4 eL_fst4 = vec4(img_e*L + 0.5);
    
    		vec4 eb_ef_eh_ed = eL_fst4 + vec4(img_abcd.y, img_fghi.x, img_fghi.z, img_abcd.w);
    
    		vec4 index11_12 = vec4(vec2(img_fghi.z*L + img_fghi.w + 0.5, eb_ef_eh_ed.y), vec2(img_fghi.x*L + img_abcd.z + 0.5, eb_ef_eh_ed.x))*step_lut; //1
    		vec4 index13_14 = vec4(vec2(img_abcd.y*L + img_abcd.x + 0.5, eb_ef_eh_ed.w), vec2(img_abcd.w*L + img_fghi.y + 0.5, eb_ef_eh_ed.z))*step_lut;
    
    		vec4 index21_22 = index11_12 + vec4(0.0, L, 0.0, L)*step_lut;
    		vec4 index31_32 = index21_22 + vec4(0.0, 1.0, 0.0, 1.0)*step_lut;
    		vec4 index41_42 = index31_32 + vec4(L, 0.0, L, 0.0)*step_lut;
    		vec4 index51_52 = index41_42 + vec4(1.0, 0.0, 1.0, 0.0)*step_lut;

    		vec4 index23_24 = index13_14 + vec4(0.0, L, 0.0, L)*step_lut;
    		vec4 index33_34 = index23_24 + vec4(0.0, 1.0, 0.0, 1.0)*step_lut;
    		vec4 index43_44 = index33_34 + vec4(L, 0.0, L, 0.0)*step_lut;
    		vec4 index53_54 = index43_44 + vec4(1.0, 0.0, 1.0, 0.0)*step_lut;
    
    		vec4 w1234_1_1 = vec4(16.0, f_e, f_fghi.x, f_fghi.z);
    		vec4 w1234_1_2 = vec4(f_e, f_fghi.x, f_fghi.z, f_fghi.w);
    		vec4 w1234_1 = w1234_1_1 - w1234_1_2; //weights 1, vec4
    		//float w51 = f_fghi.w;//
    
    		vec4 w1234_2_1 = vec4(16.0, f_e, f_abcd.y, f_fghi.x);
    		vec4 w1234_2_2 = vec4(f_e, f_abcd.y, f_fghi.x, f_abcd.z);
    		vec4 w1234_2 = w1234_2_1 - w1234_2_2; 
    		//float w52 = f_abcd.z;//
    
    		vec4 w1234_3_1 = vec4(16.0, f_e, f_abcd.w, f_abcd.y);
    		vec4 w1234_3_2 = vec4(f_e, f_abcd.w, f_abcd.y, f_abcd.x);
    		vec4 w1234_3 = w1234_3_1 - w1234_3_2; 
    		//float w53 = f_abcd.x;//
    
    		vec4 w1234_4_1 = vec4(16.0, f_e, f_fghi.z, f_abcd.w);
    		vec4 w1234_4_2 = vec4(f_e, f_fghi.z, f_abcd.w, f_fghi.y);
    		vec4 w1234_4 = w1234_4_1 - w1234_4_2; //weights 1, vec4
    		//float w54 = f_fghi.y;//

			vec4 w5 = vec4(f_fghi.w, f_abcd.z, f_abcd.x, f_fghi.y);
        
    		float color_res11 = texture2D(inputLUTTexture, index11_12.xy)[int(indexVec4[0])] -err;
    		float color_res21 = texture2D(inputLUTTexture, index21_22.xy)[int(indexVec4[0])] -err;
    		float color_res31 = texture2D(inputLUTTexture, index31_32.xy)[int(indexVec4[0])] -err;
    		float color_res41 = texture2D(inputLUTTexture, index41_42.xy)[int(indexVec4[0])] -err;
    		float color_res51 = texture2D(inputLUTTexture, index51_52.xy)[int(indexVec4[0])] -err;

    		float color_res12 = texture2D(inputLUTTexture, index11_12.zw)[int(indexVec4[1])] -err;
    		float color_res22 = texture2D(inputLUTTexture, index21_22.zw)[int(indexVec4[1])] -err;
    		float color_res32 = texture2D(inputLUTTexture, index31_32.zw)[int(indexVec4[1])] -err;
    		float color_res42 = texture2D(inputLUTTexture, index41_42.zw)[int(indexVec4[1])] -err;
    		float color_res52 = texture2D(inputLUTTexture, index51_52.zw)[int(indexVec4[1])] -err;

    		float color_res13 = texture2D(inputLUTTexture, index13_14.xy)[int(indexVec4[2])] -err;
    		float color_res23 = texture2D(inputLUTTexture, index23_24.xy)[int(indexVec4[2])] -err;
    		float color_res33 = texture2D(inputLUTTexture, index33_34.xy)[int(indexVec4[2])] -err;
    		float color_res43 = texture2D(inputLUTTexture, index43_44.xy)[int(indexVec4[2])] -err;
    		float color_res53 = texture2D(inputLUTTexture, index53_54.xy)[int(indexVec4[2])] -err;

    		float color_res14 = texture2D(inputLUTTexture, index13_14.zw)[int(indexVec4[3])] -err;
    		float color_res24 = texture2D(inputLUTTexture, index23_24.zw)[int(indexVec4[3])] -err;
    		float color_res34 = texture2D(inputLUTTexture, index33_34.zw)[int(indexVec4[3])] -err;
    		float color_res44 = texture2D(inputLUTTexture, index43_44.zw)[int(indexVec4[3])] -err;
    		float color_res54 = texture2D(inputLUTTexture, index53_54.zw)[int(indexVec4[3])] -err;

			vec4 color_res5_ = vec4(color_res51, color_res52, color_res53, color_res54);
			color_res5_ = w5 * color_res5_;

    		vec4 data1234_1 = vec4(color_res11, color_res21, color_res31, color_res41);
    		vec4 data_res1234_1 = w1234_1 * data1234_1;
    		float color_res1 = data_res1234_1.x + data_res1234_1.y + data_res1234_1.z + data_res1234_1.w + color_res5_.x;
    
    		vec4 data1234_2 = vec4(color_res12, color_res22, color_res32, color_res42);
    		vec4 data_res1234_2 = w1234_2 * data1234_2;
    		float color_res2 = data_res1234_2.x + data_res1234_2.y + data_res1234_2.z + data_res1234_2.w + color_res5_.y;
    
    		vec4 data1234_3 = vec4(color_res13, color_res23, color_res33, color_res43);
    		vec4 data_res1234_3 = w1234_3 * data1234_3;
    		float color_res3 = data_res1234_3.x + data_res1234_3.y + data_res1234_3.z + data_res1234_3.w + color_res5_.z;
        
    		vec4 data1234_4 = vec4(color_res14, color_res24, color_res34, color_res44);
    		vec4 data_res1234_4 = w1234_4 * data1234_4;
    		float color_res4 = data_res1234_4.x + data_res1234_4.y + data_res1234_4.z + data_res1234_4.w + color_res5_.w;
        
    		float color_res = color_res1 + color_res2 + color_res3 + color_res4;
    		color_res /= 16.0;
    		gl_FragColor = vec4(vec3(color_res), 1.0);
		}
		);

	}

	bool SRLutOpenGL::CheckCondition(VideoFrame& input_frame) {
		src_width = input_frame.width();
		src_height = input_frame.height();
		if ((src_width == 640 && src_height == 360) || (src_width == 360 && src_height == 640)) { // only support 360p
			return true;
		}
		return false;
	}
	//
	void SRLutOpenGL::LoadLut() {
		if (!lut_data_texture) {
			OpenGLTexture::CreateInfo lut_data_info;
			lut_data_info.mContext = mOpenGLContext;
			lut_data_info.textureId = 0;
			lut_data_info.width = 289;
			lut_data_info.height = 289;
			lut_data_info.bindFrameBuffer = false;
			lut_data_info.type = GLTextureType::kRGBTexture;
			lut_data_info.dataType = GLTextureDataType::kRGBDataType;
			lut_data_info.description = "LutData-RGBA";
			lut_data_texture = OpenGLTexture::CreateTexture(lut_data_info);
			glBindTexture(GL_TEXTURE_2D, lut_data_texture->GetTextureId());
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 289, 289, 0, GL_RGBA, GL_UNSIGNED_BYTE, lutData);
			glBindTexture(GL_TEXTURE_2D, 0);
			RTC_LOG(LS_INFO) << "[Process] SRLUTOpenGL: Load LUT";
		}
	}

	void SRLutOpenGL::InitEnv() {
		if (!mOpenGLContext) {
			mOpenGLContext = (OpenGLContext*)GetEGLContext();
		}
		mOpenGLContext->MakeCurrent();  

		//if (!mInitLut) {
		LoadLut();
		//}
		//lut_program.reset(new OpenGLProgram(kLutVertexShaderString.c_str(), kLutFragmentShaderString.c_str()));
		lut_program.reset(new OpenGLProgram(kLutVertexShaderString.c_str(), kLutOptFragmentShaderString.c_str()));

		//init framebuffer
		UpdateTexture(dst_width, dst_height);
		RTC_LOG(LS_INFO) << "[Process] SRLUTOpenGL: Init Env";
		mInit = true;
	}

	void SRLutOpenGL::UpdateTexture (int width, int height){ //should be input dst_size
		if (width == tex_width && height == tex_height){
			return;
		}
		OpenGLTexture::CreateInfo lut_info;
		lut_info.mContext = mOpenGLContext;
		lut_info.textureId = 0;
		lut_info.width = dst_width;   
		lut_info.height = dst_height;
		lut_info.bindFrameBuffer = true;  
		lut_info.type = GLTextureType::kRGBTexture;
		lut_info.dataType = GLTextureDataType::kYDataType;  
		lut_info.description = "LutOpenGL-RGB";
		lut_texture_ = OpenGLTexture::CreateTexture(lut_info);
		tex_width = lut_info.width;
		tex_height = lut_info.height;
		RTC_LOG(LS_INFO) << "Update texture, width: " <<tex_width <<", height: "<<tex_height; 

	}

	rtc::scoped_refptr<VideoFrameBuffer> SRLutOpenGL::ProcessFilter(VideoFrame& input_frame) {
		rtc::scoped_refptr<VideoFrameBuffer> input_buffer_ = input_frame.video_frame_buffer();

		src_width = input_frame.width();
		src_height = input_frame.height();
		dst_width = src_width * 2;
		dst_height = src_height * 2;

		if ((src_width != 640 || src_height != 360) && (src_width != 360 || src_height != 640)) {  // only support 360p 	
			//RTC_LOG(LS_INFO) << "[Process] Only support 360p, break\n";
			return input_buffer_;
		}

		webrtc::VideoFrameBuffer::Type type = input_buffer_->type();
		if (type != webrtc::VideoFrameBuffer::Type::kNative) {
			// RTC_LOG(LS_ERROR) << "[Process] LanczosOpenGL : input buffer is not kNative! \n";
			return input_buffer_;
		}
		
		if(!mOpenGLContext){
        	mOpenGLContext = (OpenGLContext*)GetEGLContext();
    	}
    	mOpenGLContext->MakeCurrent();

		if (!mInit) {
			InitEnv();
		}

		if (!lut_data_texture) return input_buffer_;

		UpdateTexture(dst_width, dst_height);

		void* y_texture = input_buffer_->yTexture();
		void* u_texture = input_buffer_->uTexture();
		void* v_texture = input_buffer_->vTexture();

		int yTextureId = *static_cast<int*>(y_texture);
		int uTextureId = *static_cast<int*>(u_texture);
		int vTextureId = *static_cast<int*>(v_texture);

		if (yTextureId <= 0) {
			RTC_LOG(LS_ERROR) << "[Process] SRLUTOpenGL : input texture is error! \n";
			return input_buffer_;
		}

		jni::AndroidVideoBuffer* android_buffer = static_cast<jni::AndroidVideoBuffer*>(input_buffer_.get());

		lut_program->CheckGLError("[Process] SRLutOpenGL : error 0! \n"); //lut_program: shaders set in init

		//20220307

		if(!mOpenGLContext){
        	mOpenGLContext = (OpenGLContext*)GetEGLContext();
    	}
    	mOpenGLContext->MakeCurrent();

		lut_program->UseProgram();
		GLuint position = lut_program->GetAttribLocation("position");
		GLuint texcoord = lut_program->GetAttribLocation("texCoord");
		GLuint srcUniformTexture = lut_program->GetUniformLocation("inputImageTexture");
		GLuint lutUniformTexture = lut_program->GetUniformLocation("inputLUTTexture");
		GLuint srcSizewUniform = lut_program->GetUniformLocation("srcSize_w");
		GLuint srcSizehUniform = lut_program->GetUniformLocation("srcSize_h");
		GLuint dstSizewUniform = lut_program->GetUniformLocation("dstSize_w");
		GLuint dstSizehUniform = lut_program->GetUniformLocation("dstSize_h");
		GLuint lutStep = lut_program->GetUniformLocation("step_lut");

		glBindFramebuffer(GL_FRAMEBUFFER, lut_texture_->GetFrameBufferId());
		glClear(GL_COLOR_BUFFER_BIT);   
		glClearColor(0, 0, 0, 1);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, yTextureId);  
		glUniform1i(srcUniformTexture, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, lut_data_texture->GetTextureId());  
		glUniform1i(lutUniformTexture, 1);

		glUniform1f(srcSizewUniform, (float)src_width);
		glUniform1f(srcSizehUniform, (float)src_height);
		glUniform1f(dstSizewUniform, (float)dst_width);
		glUniform1f(dstSizehUniform, (float)dst_height);
		glUniform1f(lutStep, 1.0/289.0);
		
		glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, RectangleBuffer);
		glEnableVertexAttribArray(position);
		glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 0, RectangleTextureBuffer);
		glEnableVertexAttribArray(texcoord);
		glViewport(0, 0, dst_width, dst_height);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		glDisableVertexAttribArray(position);
		glDisableVertexAttribArray(texcoord);

		glBindTexture(GL_TEXTURE_2D, 0);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glFlush();

		android_buffer->setTextureIds(lut_texture_->GetTextureId(), uTextureId, vTextureId);
		android_buffer->updateSize(dst_width, dst_height);

		return input_buffer_;
	}

	rtc::scoped_refptr<VideoFrameBuffer> SRLutOpenGL::ProcessFilter(std::unique_ptr<VideoProcessData>& process_param) {
		if (process_param &&
			process_param->GetPostProcessInputParam().input_frame) {
			return ProcessFilter(*(process_param->GetPostProcessInputParam().input_frame));
		}
		return nullptr;
	}

}// namespace webrtc
