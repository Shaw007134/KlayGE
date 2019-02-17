// OGLShaderObject.cpp
// KlayGE OpenGL shader对象类 实现文件
// Ver 3.12.0
// 版权所有(C) 龚敏敏, 2006-2010
// Homepage: http://www.klayge.org
//
// 3.11.0
// Geometry shader to GLSL compiler works (2010.8.16)
// Reuse generated GLSL between passes (2010.9.30)
//
// 3.9.0
// Cg载入后编译成GLSL使用 (2009.4.26)
//
// 3.7.0
// 改为直接传入RenderEffect (2008.7.4)
//
// 3.5.0
// 初次建立 (2006.11.2)
//
// 修改记录
//////////////////////////////////////////////////////////////////////////////////

#include <KlayGE/KlayGE.hpp>
#include <KFL/CXX17/string_view.hpp>
#include <KFL/ErrorHandling.hpp>
#include <KFL/Util.hpp>
#include <KFL/ResIdentifier.hpp>
#include <KlayGE/Context.hpp>
#include <KFL/Math.hpp>
#include <KFL/Matrix.hpp>
#include <KlayGE/RenderEffect.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderFactory.hpp>
#include <KFL/CustomizedStreamBuf.hpp>
#include <KFL/Hash.hpp>

#include <cstdio>
#include <string>
#include <algorithm>
#include <cstring>

#include <boost/assert.hpp>

#include <glloader/glloader.h>

#include <DXBC2GLSL/DXBC2GLSL.hpp>

#ifndef D3DCOMPILE_SKIP_OPTIMIZATION
#define D3DCOMPILE_SKIP_OPTIMIZATION 0x00000004
#endif
#ifndef D3DCOMPILE_PREFER_FLOW_CONTROL
#define D3DCOMPILE_PREFER_FLOW_CONTROL 0x00000400
#endif
#ifndef D3DCOMPILE_ENABLE_STRICTNESS
#define D3DCOMPILE_ENABLE_STRICTNESS 0x00000800
#endif

#include <KlayGE/OpenGL/OGLRenderFactory.hpp>
#include <KlayGE/OpenGL/OGLRenderEngine.hpp>
#include <KlayGE/OpenGL/OGLRenderView.hpp>
#include <KlayGE/OpenGL/OGLMapping.hpp>
#include <KlayGE/OpenGL/OGLTexture.hpp>
#include <KlayGE/OpenGL/OGLRenderStateObject.hpp>
#include <KlayGE/OpenGL/OGLGraphicsBuffer.hpp>
#include <KlayGE/OpenGL/OGLShaderObject.hpp>

namespace
{
	using namespace KlayGE;

	template <typename SrcType>
	class SetOGLShaderParameter
	{
	};

	template <>
	class SetOGLShaderParameter<GraphicsBufferPtr>
	{
	public:
		SetOGLShaderParameter(std::vector<TextureBind>& buffers,
					std::vector<GLuint>& gl_bind_targets, std::vector<GLuint>& gl_bind_textures, std::vector<GLuint>& gl_bind_samplers,
					GLint location, GLuint stage,
					RenderEffectParameter* buff_param)
			: buffers_(&buffers),
				gl_bind_targets_(&gl_bind_targets), gl_bind_textures_(&gl_bind_textures), gl_bind_samplers_(&gl_bind_samplers),
				location_(location), stage_(stage), buff_param_(buff_param)
		{
		}

		void operator()()
		{
			ShaderResourceViewPtr srv;
			buff_param_->Value(srv);
			(*buffers_)[stage_].buff_srv = srv;

			auto* gl_srv = checked_cast<OGLShaderResourceView*>(srv.get());
			if (srv)
			{
				gl_srv->RetrieveGLTargetTexture((*gl_bind_targets_)[stage_], (*gl_bind_textures_)[stage_]);
			}
			else
			{
				(*gl_bind_targets_)[stage_] = GL_TEXTURE_BUFFER;
				(*gl_bind_textures_)[stage_] = 0;
			}
			(*gl_bind_samplers_)[stage_] = 0;

			OGLRenderEngine& re = *checked_cast<OGLRenderEngine*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
			re.Uniform1i(location_, stage_);
		}

	private:
		std::vector<TextureBind>* buffers_;
		std::vector<GLuint>* gl_bind_targets_;
		std::vector<GLuint>* gl_bind_textures_;
		std::vector<GLuint>* gl_bind_samplers_;
		GLint location_;
		GLuint stage_;
		RenderEffectParameter* buff_param_;
	};

	template <>
	class SetOGLShaderParameter<TexturePtr>
	{
	public:
		SetOGLShaderParameter(std::vector<TextureBind>& samplers,
					std::vector<GLuint>& gl_bind_targets, std::vector<GLuint>& gl_bind_textures, std::vector<GLuint>& gl_bind_samplers,
					GLint location, GLuint stage,
					RenderEffectParameter* tex_param, RenderEffectParameter* sampler_param)
			: samplers_(&samplers),
				gl_bind_targets_(&gl_bind_targets), gl_bind_textures_(&gl_bind_textures), gl_bind_samplers_(&gl_bind_samplers),
				location_(location), stage_(stage), tex_param_(tex_param), sampler_param_(sampler_param)
		{
		}

		void operator()()
		{
			ShaderResourceViewPtr srv;
			tex_param_->Value(srv);
			(*samplers_)[stage_].tex_srv = srv;

			sampler_param_->Value((*samplers_)[stage_].sampler);

			auto* gl_srv = checked_cast<OGLShaderResourceView*>(srv.get());
			if (srv)
			{
				auto* gl_sampler = checked_cast<OGLSamplerStateObject*>((*samplers_)[stage_].sampler.get());

				gl_srv->RetrieveGLTargetTexture((*gl_bind_targets_)[stage_], (*gl_bind_textures_)[stage_]);
				(*gl_bind_samplers_)[stage_] = gl_sampler->GLSampler();
			}
			else
			{
				(*gl_bind_targets_)[stage_] = GL_TEXTURE_2D;
				(*gl_bind_textures_)[stage_] = 0;
				(*gl_bind_samplers_)[stage_] = 0;
			}

			OGLRenderEngine& re = *checked_cast<OGLRenderEngine*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
			re.Uniform1i(location_, stage_);
		}

	private:
		std::vector<TextureBind>* samplers_;
		std::vector<GLuint>* gl_bind_targets_;
		std::vector<GLuint>* gl_bind_textures_;
		std::vector<GLuint>* gl_bind_samplers_;
		GLint location_;
		GLuint stage_;
		RenderEffectParameter* tex_param_;
		RenderEffectParameter* sampler_param_;
	};

	void PrintGlslErrorAtLine(std::string const& glsl, int err_line)
	{
		MemInputStreamBuf glsl_buff(glsl.data(), glsl.size());
		std::istream iss(&glsl_buff);
		std::string s;
		int line = 1;
		LogError() << "..." << std::endl;
		while (iss)
		{
			std::getline(iss, s);
			if ((line - err_line > -3) && (line - err_line < 3))
			{
				LogError() << line << ' ' << s << std::endl;
			}
			++line;
		}
		LogError() << "..." << std::endl;
	}

	void PrintGlslError(std::string const& glsl, std::string_view info)
	{
		OGLRenderEngine& re = *checked_cast<OGLRenderEngine*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());

		if (re.HackForIntel())
		{
			MemInputStreamBuf info_buff(info.data(), info.size());
			std::istream err_iss(&info_buff);
			std::string err_str;
			while (err_iss)
			{
				std::getline(err_iss, err_str);
				if (!err_str.empty())
				{
					std::string::size_type pos = err_str.find(": 0:");
					if (pos != std::string::npos)
					{
						pos += 4;
						std::string::size_type pos2 = err_str.find(':', pos + 1);
						std::string part_err_str = err_str.substr(pos, pos2 - pos);
						PrintGlslErrorAtLine(glsl, std::stoi(part_err_str));
					}

					LogError() << err_str << std::endl << std::endl;
				}
			}
		}
		else if (re.HackForNV())
		{
			MemInputStreamBuf info_buff(info.data(), info.size());
			std::istream err_iss(&info_buff);
			std::string err_str;
			while (err_iss)
			{
				std::getline(err_iss, err_str);
				if (!err_str.empty())
				{
					std::string::size_type pos = err_str.find(") : error");
					if (pos != std::string::npos)
					{
						std::string::size_type pos2 = err_str.find('(') + 1;
						std::string part_err_str = err_str.substr(pos2, pos - pos2);
						PrintGlslErrorAtLine(glsl, std::stoi(part_err_str));
					}

					LogError() << err_str << std::endl << std::endl;
				}
			}
		}
		else
		{
			MemInputStreamBuf glsl_buff(glsl.data(), glsl.size());
			std::istream iss(&glsl_buff);
			std::string s;
			int line = 1;
			while (iss)
			{
				std::getline(iss, s);
				LogError() << line << ' ' << s << std::endl;
				++line;
			}

			LogError() << info << std::endl << std::endl;
		}
	}
}

namespace KlayGE
{
	OGLShaderStageObject::OGLShaderStageObject(ShaderObject::ShaderType stage, GLenum gl_shader_type)
		: stage_(stage), gl_shader_type_(gl_shader_type)
	{
	}

	OGLShaderStageObject::~OGLShaderStageObject()
	{
		if (gl_shader_ != 0)
		{
			glDeleteShader(gl_shader_);
		}
	}

	void OGLShaderStageObject::StreamIn(RenderEffect const& effect,
		std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids, std::vector<uint8_t> const& native_shader_block)
	{
		auto const& sd = effect.GetShaderDesc(shader_desc_ids[stage_]);

		shader_func_name_ = sd.func_name;

		is_validate_ = false;
		if (native_shader_block.size() >= 24)
		{
			MemInputStreamBuf native_shader_buff(native_shader_block.data(), native_shader_block.size());
			std::istream native_shader_stream(&native_shader_buff);

			is_validate_ = true;

			uint32_t len32;
			native_shader_stream.read(reinterpret_cast<char*>(&len32), sizeof(len32));
			len32 = LE2Native(len32);
			glsl_src_.resize(len32, '\0');
			native_shader_stream.read(&glsl_src_[0], len32);

			uint16_t num16;
			native_shader_stream.read(reinterpret_cast<char*>(&num16), sizeof(num16));
			num16 = LE2Native(num16);
			pnames_.resize(num16);
			for (size_t i = 0; i < num16; ++i)
			{
				uint8_t len8;
				native_shader_stream.read(reinterpret_cast<char*>(&len8), sizeof(len8));

				pnames_[i].resize(len8);
				native_shader_stream.read(&pnames_[i][0], len8);
			}

			native_shader_stream.read(reinterpret_cast<char*>(&num16), sizeof(num16));
			num16 = LE2Native(num16);
			glsl_res_names_.resize(num16);
			for (size_t i = 0; i < num16; ++i)
			{
				uint8_t len8;
				native_shader_stream.read(reinterpret_cast<char*>(&len8), sizeof(len8));

				glsl_res_names_[i].resize(len8);
				native_shader_stream.read(&glsl_res_names_[i][0], len8);
			}

			native_shader_stream.read(reinterpret_cast<char*>(&num16), sizeof(num16));
			num16 = LE2Native(num16);
			for (size_t i = 0; i < num16; ++i)
			{
				uint8_t len8;
				native_shader_stream.read(reinterpret_cast<char*>(&len8), sizeof(len8));

				std::string tex_name;
				tex_name.resize(len8);
				native_shader_stream.read(&tex_name[0], len8);

				native_shader_stream.read(reinterpret_cast<char*>(&len8), sizeof(len8));

				std::string sampler_name;
				sampler_name.resize(len8);
				native_shader_stream.read(&sampler_name[0], len8);

				tex_sampler_pairs_.push_back({tex_name, sampler_name});
			}

			this->StageSpecificStreamIn(native_shader_stream);

			this->CreateHwShader();
		}
	}

	void OGLShaderStageObject::StreamOut(std::ostream& os)
	{
		std::vector<char> native_shader_block;

		if (!glsl_src_.empty())
		{
			VectorOutputStreamBuf native_shader_buff(native_shader_block);
			std::ostream oss(&native_shader_buff);

			uint32_t len32 = Native2LE(static_cast<uint32_t>(glsl_src_.size()));
			oss.write(reinterpret_cast<char const*>(&len32), sizeof(len32));
			oss.write(&glsl_src_[0], glsl_src_.size());

			uint16_t num16 = Native2LE(static_cast<uint16_t>(pnames_.size()));
			oss.write(reinterpret_cast<char const*>(&num16), sizeof(num16));
			for (size_t i = 0; i < pnames_.size(); ++i)
			{
				uint8_t len8 = static_cast<uint8_t>(pnames_[i].size());
				oss.write(reinterpret_cast<char const*>(&len8), sizeof(len8));
				oss.write(&pnames_[i][0], pnames_[i].size());
			}

			num16 = Native2LE(static_cast<uint16_t>(glsl_res_names_.size()));
			oss.write(reinterpret_cast<char const*>(&num16), sizeof(num16));
			for (size_t i = 0; i < glsl_res_names_.size(); ++i)
			{
				uint8_t len8 = static_cast<uint8_t>(glsl_res_names_[i].size());
				oss.write(reinterpret_cast<char const*>(&len8), sizeof(len8));
				oss.write(&glsl_res_names_[i][0], glsl_res_names_[i].size());
			}

			num16 = Native2LE(static_cast<uint16_t>(tex_sampler_pairs_.size()));
			oss.write(reinterpret_cast<char const*>(&num16), sizeof(num16));
			for (size_t i = 0; i < num16; ++i)
			{
				uint8_t len8 = static_cast<uint8_t>(tex_sampler_pairs_[i].first.size());
				oss.write(reinterpret_cast<char const*>(&len8), sizeof(len8));
				oss.write(&tex_sampler_pairs_[i].first[0], len8);

				len8 = static_cast<uint8_t>(tex_sampler_pairs_[i].second.size());
				oss.write(reinterpret_cast<char const*>(&len8), sizeof(len8));
				oss.write(&tex_sampler_pairs_[i].second[0], len8);
			}

			this->StageSpecificStreamOut(oss);
		}

		uint32_t len = static_cast<uint32_t>(native_shader_block.size());
		{
			uint32_t tmp = Native2LE(len);
			os.write(reinterpret_cast<char const *>(&tmp), sizeof(tmp));
		}
		if (len > 0)
		{
			os.write(reinterpret_cast<char const *>(&native_shader_block[0]), len * sizeof(native_shader_block[0]));
		}
	}

	void OGLShaderStageObject::AttachShader(RenderEffect const& effect, RenderTechnique const& tech, RenderPass const& pass,
		std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids)
	{
		ShaderDesc const& sd = effect.GetShaderDesc(shader_desc_ids[stage_]);

		shader_func_name_ = sd.func_name;

		bool has_gs = false;
		if (!effect.GetShaderDesc(shader_desc_ids[ShaderObject::ST_GeometryShader]).func_name.empty())
		{
			has_gs = true;
		}
		bool has_ps = false;
		if (!effect.GetShaderDesc(shader_desc_ids[ShaderObject::ST_PixelShader]).func_name.empty())
		{
			has_ps = true;
		}

		is_validate_ = true;
		switch (stage_)
		{
		case ShaderObject::ST_VertexShader:
		case ShaderObject::ST_PixelShader:
		case ShaderObject::ST_GeometryShader:
		case ShaderObject::ST_HullShader:
		case ShaderObject::ST_DomainShader:
			break;

		default:
			is_validate_ = false;
			break;
		}

		if (is_validate_)
		{
			auto const& re = *checked_cast<OGLRenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
			auto const& caps = re.DeviceCaps();

			std::string_view const shader_profile = this->GetShaderProfile(effect, shader_desc_ids[stage_]);
			is_validate_ = !shader_profile.empty();

			if (is_validate_)
			{
				std::string err_msg;
				std::vector<std::pair<char const*, char const*>> macros;
				macros.emplace_back("KLAYGE_DXBC2GLSL", "1");
				macros.emplace_back("KLAYGE_OPENGL", "1");
				if (!caps.TextureFormatSupport(EF_BC5) || !caps.TextureFormatSupport(EF_BC5_SRGB))
				{
					macros.emplace_back("KLAYGE_BC5_AS_AG", "1");
				}
				if (!caps.TextureFormatSupport(EF_BC4) || !caps.TextureFormatSupport(EF_BC4_SRGB))
				{
					macros.emplace_back("KLAYGE_BC4_AS_G", "1");
				}
				macros.emplace_back("KLAYGE_FRAG_DEPTH", "1");

				uint32_t const flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PREFER_FLOW_CONTROL | D3DCOMPILE_SKIP_OPTIMIZATION;
				std::vector<uint8_t> code =
					ShaderObject::CompileToDXBC(stage_, effect, tech, pass, macros, sd.func_name.c_str(), shader_profile.data(), flags);
				if (code.empty())
				{
					is_validate_ = false;
				}
				else
				{
					try
					{
						GLSLVersion gsv;
						if (glloader_GL_VERSION_4_6())
						{
							gsv = GSV_460;
						}
						else if (glloader_GL_VERSION_4_5())
						{
							gsv = GSV_450;
						}
						else if (glloader_GL_VERSION_4_4())
						{
							gsv = GSV_440;
						}
						else if (glloader_GL_VERSION_4_3())
						{
							gsv = GSV_430;
						}
						else if (glloader_GL_VERSION_4_2())
						{
							gsv = GSV_420;
						}
						else // if (glloader_GL_VERSION_4_1())
						{
							gsv = GSV_410;
						}

						DXBC2GLSL::DXBC2GLSL dxbc2glsl;
						uint32_t rules = DXBC2GLSL::DXBC2GLSL::DefaultRules(gsv);
						rules &= ~GSR_UniformBlockBinding;
						dxbc2glsl.FeedDXBC(&code[0], has_gs, has_ps, static_cast<ShaderTessellatorPartitioning>(this->DsPartitioning()),
							static_cast<ShaderTessellatorOutputPrimitive>(this->DsOutputPrimitive()), gsv, rules);
						glsl_src_ = dxbc2glsl.GLSLString();
						pnames_.clear();
						glsl_res_names_.clear();

						for (uint32_t i = 0; i < dxbc2glsl.NumCBuffers(); ++i)
						{
							for (uint32_t j = 0; j < dxbc2glsl.NumVariables(i); ++j)
							{
								if (dxbc2glsl.VariableUsed(i, j))
								{
									pnames_.push_back(dxbc2glsl.VariableName(i, j));
									glsl_res_names_.push_back(dxbc2glsl.VariableName(i, j));
								}
							}
						}

						std::vector<char const*> tex_names;
						std::vector<char const*> sampler_names;
						for (uint32_t i = 0; i < dxbc2glsl.NumResources(); ++i)
						{
							if (dxbc2glsl.ResourceUsed(i))
							{
								char const* res_name = dxbc2glsl.ResourceName(i);

								if (SIT_TEXTURE == dxbc2glsl.ResourceType(i))
								{
									if (SSD_BUFFER == dxbc2glsl.ResourceDimension(i))
									{
										pnames_.push_back(res_name);
										glsl_res_names_.push_back(res_name);
									}
									else
									{
										tex_names.push_back(res_name);
									}
								}
								else if (SIT_SAMPLER == dxbc2glsl.ResourceType(i))
								{
									sampler_names.push_back(res_name);
								}
							}
						}

						for (size_t i = 0; i < tex_names.size(); ++i)
						{
							for (size_t j = 0; j < sampler_names.size(); ++j)
							{
								std::string const combined_sampler_name = std::string(tex_names[i]) + "_" + sampler_names[j];
								tex_sampler_pairs_.push_back({tex_names[i], sampler_names[j]});

								pnames_.push_back(combined_sampler_name);
								glsl_res_names_.push_back(combined_sampler_name);
							}
						}

						this->StageSpecificAttachShader(dxbc2glsl);

						this->CreateHwShader();
					}
					catch (std::exception& ex)
					{
						is_validate_ = false;

						LogError() << "Error(s) in conversion: " << tech.Name() << "/" << pass.Name() << "/" << sd.func_name << std::endl;
						LogError() << ex.what() << std::endl;
						LogError() << "Please send this information and your shader to webmaster at klayge.org. We'll fix this ASAP."
								   << std::endl;
					}
				}
			}
		}
	}

	void OGLShaderStageObject::CreateHwShader()
	{
		char const* glsl = glsl_src_.c_str();
		gl_shader_ = glCreateShader(gl_shader_type_);
		if (0 == gl_shader_)
		{
			is_validate_ = false;
		}
		else
		{
			glShaderSource(gl_shader_, 1, &glsl, nullptr);

			glCompileShader(gl_shader_);

			GLint compiled = false;
			glGetShaderiv(gl_shader_, GL_COMPILE_STATUS, &compiled);
			if (!compiled)
			{
				LogError() << "Error when compiling GLSL " << shader_func_name_ << ":" << std::endl;

				GLint len = 0;
				glGetShaderiv(gl_shader_, GL_INFO_LOG_LENGTH, &len);
				if (len > 0)
				{
					std::vector<char> info(len);
					glGetShaderInfoLog(gl_shader_, len, &len, &info[0]);
					PrintGlslError(glsl_src_, std::string_view(info.data(), info.size()));
				}

				is_validate_ = false;
			}
		}
	}


	OGLVertexShaderStageObject::OGLVertexShaderStageObject() : OGLShaderStageObject(ShaderObject::ST_VertexShader, GL_VERTEX_SHADER)
	{
	}

	std::string_view OGLVertexShaderStageObject::GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const
	{
		std::string_view shader_profile = effect.GetShaderDesc(shader_desc_id).profile;
		if (shader_profile == "auto")
		{
			shader_profile = "vs_5_0";
		}

		return shader_profile;
	}

	void OGLVertexShaderStageObject::StageSpecificStreamIn(std::istream& native_shader_stream)
	{
		uint8_t num8;
		native_shader_stream.read(reinterpret_cast<char*>(&num8), sizeof(num8));
		usages_.resize(num8);
		for (size_t i = 0; i < num8; ++i)
		{
			uint8_t veu;
			native_shader_stream.read(reinterpret_cast<char*>(&veu), sizeof(veu));

			usages_[i] = static_cast<VertexElementUsage>(veu);
		}

		native_shader_stream.read(reinterpret_cast<char*>(&num8), sizeof(num8));
		if (num8 > 0)
		{
			usage_indices_.resize(num8);
			native_shader_stream.read(reinterpret_cast<char*>(&usage_indices_[0]), num8 * sizeof(usage_indices_[0]));
		}

		native_shader_stream.read(reinterpret_cast<char*>(&num8), sizeof(num8));
		glsl_attrib_names_.resize(num8);
		for (size_t i = 0; i < num8; ++i)
		{
			uint8_t len8;
			native_shader_stream.read(reinterpret_cast<char*>(&len8), sizeof(len8));

			glsl_attrib_names_[i].resize(len8);
			native_shader_stream.read(&glsl_attrib_names_[i][0], len8);
		}
	}

	void OGLVertexShaderStageObject::StageSpecificStreamOut(std::ostream& os)
	{
		uint8_t num8 = static_cast<uint8_t>(usages_.size());
		os.write(reinterpret_cast<char const*>(&num8), sizeof(num8));
		for (size_t i = 0; i < usages_.size(); ++i)
		{
			uint8_t veu = static_cast<uint8_t>(usages_[i]);
			os.write(reinterpret_cast<char const*>(&veu), sizeof(veu));
		}

		num8 = static_cast<uint8_t>(usage_indices_.size());
		os.write(reinterpret_cast<char const*>(&num8), sizeof(num8));
		if (!usage_indices_.empty())
		{
			os.write(reinterpret_cast<char const*>(&usage_indices_[0]), usage_indices_.size() * sizeof(usage_indices_[0]));
		}

		num8 = static_cast<uint8_t>(glsl_attrib_names_.size());
		os.write(reinterpret_cast<char const*>(&num8), sizeof(num8));
		for (size_t i = 0; i < glsl_attrib_names_.size(); ++i)
		{
			uint8_t len8 = static_cast<uint8_t>(glsl_attrib_names_[i].size());
			os.write(reinterpret_cast<char const*>(&len8), sizeof(len8));
			os.write(&glsl_attrib_names_[i][0], glsl_attrib_names_[i].size());
		}
	}

	void OGLVertexShaderStageObject::StageSpecificAttachShader(DXBC2GLSL::DXBC2GLSL const& dxbc2glsl)
	{
		for (uint32_t i = 0; i < dxbc2glsl.NumInputParams(); ++i)
		{
			if (dxbc2glsl.InputParam(i).mask != 0)
			{
				std::string semantic = dxbc2glsl.InputParam(i).semantic_name;
				uint32_t semantic_index = dxbc2glsl.InputParam(i).semantic_index;
				std::string glsl_param_name = semantic;
				size_t const semantic_hash = RT_HASH(semantic.c_str());

				if ((CT_HASH("SV_VertexID") != semantic_hash) && (CT_HASH("SV_InstanceID") != semantic_hash))
				{
					VertexElementUsage usage = VEU_Position;
					uint8_t usage_index = 0;
					if (CT_HASH("POSITION") == semantic_hash)
					{
						usage = VEU_Position;
						glsl_param_name = "POSITION0";
					}
					else if (CT_HASH("NORMAL") == semantic_hash)
					{
						usage = VEU_Normal;
						glsl_param_name = "NORMAL0";
					}
					else if (CT_HASH("COLOR") == semantic_hash)
					{
						if (0 == semantic_index)
						{
							usage = VEU_Diffuse;
							glsl_param_name = "COLOR0";
						}
						else
						{
							usage = VEU_Specular;
							glsl_param_name = "COLOR1";
						}
					}
					else if (CT_HASH("BLENDWEIGHT") == semantic_hash)
					{
						usage = VEU_BlendWeight;
						glsl_param_name = "BLENDWEIGHT0";
					}
					else if (CT_HASH("BLENDINDICES") == semantic_hash)
					{
						usage = VEU_BlendIndex;
						glsl_param_name = "BLENDINDICES0";
					}
					else if (0 == semantic.find("TEXCOORD"))
					{
						usage = VEU_TextureCoord;
						usage_index = static_cast<uint8_t>(semantic_index);
						glsl_param_name = "TEXCOORD" + std::to_string(semantic_index);
					}
					else if (CT_HASH("TANGENT") == semantic_hash)
					{
						usage = VEU_Tangent;
						glsl_param_name = "TANGENT0";
					}
					else if (CT_HASH("BINORMAL") == semantic_hash)
					{
						usage = VEU_Binormal;
						glsl_param_name = "BINORMAL0";
					}
					else
					{
						KFL_UNREACHABLE("Invalid semantic");
					}

					usages_.push_back(usage);
					usage_indices_.push_back(usage_index);
					glsl_attrib_names_.push_back(glsl_param_name);
				}
			}
		}
	}


	OGLPixelShaderStageObject::OGLPixelShaderStageObject() : OGLShaderStageObject(ShaderObject::ST_PixelShader, GL_FRAGMENT_SHADER)
	{
	}

	std::string_view OGLPixelShaderStageObject::GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const
	{
		std::string_view shader_profile = effect.GetShaderDesc(shader_desc_id).profile;
		if (shader_profile == "auto")
		{
			shader_profile = "ps_5_0";
		}

		return shader_profile;
	}


	OGLGeometryShaderStageObject::OGLGeometryShaderStageObject() : OGLShaderStageObject(ShaderObject::ST_GeometryShader, GL_GEOMETRY_SHADER)
	{
	}

	std::string_view OGLGeometryShaderStageObject::GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const
	{
		auto const& re = *checked_cast<OGLRenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		auto const& caps = re.DeviceCaps();
		std::string_view shader_profile = effect.GetShaderDesc(shader_desc_id).profile;
		if (caps.gs_support)
		{
			if (shader_profile == "auto")
			{
				shader_profile = "gs_5_0";
			}
		}
		else
		{
			shader_profile = std::string_view();
		}

		return shader_profile;
	}

	void OGLGeometryShaderStageObject::StageSpecificStreamIn(std::istream& native_shader_stream)
	{
		native_shader_stream.read(reinterpret_cast<char*>(&gs_input_type_), sizeof(gs_input_type_));
		gs_input_type_ = LE2Native(gs_input_type_);

		native_shader_stream.read(reinterpret_cast<char*>(&gs_output_type_), sizeof(gs_output_type_));
		gs_output_type_ = LE2Native(gs_output_type_);

		native_shader_stream.read(reinterpret_cast<char*>(&gs_max_output_vertex_), sizeof(gs_max_output_vertex_));
		gs_max_output_vertex_ = LE2Native(gs_max_output_vertex_);
	}

	void OGLGeometryShaderStageObject::StageSpecificStreamOut(std::ostream& os)
	{
		uint32_t git = Native2LE(gs_input_type_);
		os.write(reinterpret_cast<char const*>(&git), sizeof(git));

		uint32_t got = Native2LE(gs_output_type_);
		os.write(reinterpret_cast<char const*>(&got), sizeof(got));

		uint32_t gmov = Native2LE(gs_max_output_vertex_);
		os.write(reinterpret_cast<char const*>(&gmov), sizeof(gmov));
	}

	void OGLGeometryShaderStageObject::StageSpecificAttachShader(DXBC2GLSL::DXBC2GLSL const& dxbc2glsl)
	{
		switch (dxbc2glsl.GSInputPrimitive())
		{
		case SP_Point:
			gs_input_type_ = GL_POINTS;
			break;

		case SP_Line:
			gs_input_type_ = GL_LINES;
			break;

		case SP_LineAdj:
			gs_input_type_ = GL_LINES_ADJACENCY;
			break;

		case SP_Triangle:
			gs_input_type_ = GL_TRIANGLES;
			break;

		case SP_TriangleAdj:
			gs_input_type_ = GL_TRIANGLES_ADJACENCY;
			break;

		default:
			KFL_UNREACHABLE("Invalid GS input type");
		}

		switch (dxbc2glsl.GSOutputTopology(0))
		{
		case SPT_PointList:
			gs_output_type_ = GL_POINTS;
			break;

		case SPT_LineStrip:
			gs_output_type_ = GL_LINE_STRIP;
			break;

		case SPT_TriangleStrip:
			gs_output_type_ = GL_TRIANGLE_STRIP;
			break;

		default:
			KFL_UNREACHABLE("Invalid GS output topology");
		}

		gs_max_output_vertex_ = dxbc2glsl.MaxGSOutputVertex();
	}


	OGLComputeShaderStageObject::OGLComputeShaderStageObject() : OGLShaderStageObject(ShaderObject::ST_ComputeShader, GL_COMPUTE_SHADER)
	{
		is_validate_ = false;
	}

	std::string_view OGLComputeShaderStageObject::GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const
	{
		auto const& re = *checked_cast<OGLRenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		auto const& caps = re.DeviceCaps();
		std::string_view shader_profile = effect.GetShaderDesc(shader_desc_id).profile;
		if (caps.cs_support)
		{
			if (shader_profile == "auto")
			{
				shader_profile = "cs_5_0";
			}
		}
		else
		{
			shader_profile = std::string_view();
		}

		return shader_profile;
	}


	OGLHullShaderStageObject::OGLHullShaderStageObject() : OGLShaderStageObject(ShaderObject::ST_HullShader, GL_TESS_CONTROL_SHADER)
	{
	}

	std::string_view OGLHullShaderStageObject::GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const
	{
		auto const& re = *checked_cast<OGLRenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		auto const& caps = re.DeviceCaps();
		std::string_view shader_profile = effect.GetShaderDesc(shader_desc_id).profile;
		if (caps.hs_support)
		{
			if (shader_profile == "auto")
			{
				shader_profile = "hs_5_0";
			}
		}
		else
		{
			shader_profile = std::string_view();
		}

		return shader_profile;
	}

	void OGLHullShaderStageObject::StageSpecificAttachShader(DXBC2GLSL::DXBC2GLSL const& dxbc2glsl)
	{
		ds_partitioning_ = dxbc2glsl.DSPartitioning();
		ds_output_primitive_ = dxbc2glsl.DSOutputPrimitive();
	}


	OGLDomainShaderStageObject::OGLDomainShaderStageObject() : OGLShaderStageObject(ShaderObject::ST_DomainShader, GL_TESS_EVALUATION_SHADER)
	{
	}

	void OGLDomainShaderStageObject::DsParameters(uint32_t partitioning, uint32_t output_primitive)
	{
		ds_partitioning_ = partitioning;
		ds_output_primitive_ = output_primitive;
	}

	std::string_view OGLDomainShaderStageObject::GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const
	{
		auto const& re = *checked_cast<OGLRenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		auto const& caps = re.DeviceCaps();
		std::string_view shader_profile = effect.GetShaderDesc(shader_desc_id).profile;
		if (caps.ds_support)
		{
			if (shader_profile == "auto")
			{
				shader_profile = "ds_5_0";
			}
		}
		else
		{
			shader_profile = std::string_view();
		}

		return shader_profile;
	}


	OGLShaderObject::OGLShaderObject() : OGLShaderObject(MakeSharedPtr<OGLShaderObjectTemplate>())
	{
	}

	OGLShaderObject::OGLShaderObject(std::shared_ptr<OGLShaderObjectTemplate> const& so_template) : so_template_(so_template)
	{
		has_discard_ = false;
		has_tessellation_ = false;
		is_shader_validate_.fill(true);

		glsl_program_ = glCreateProgram();
	}

	OGLShaderObject::~OGLShaderObject()
	{
		glDeleteProgram(glsl_program_);
	}

	bool OGLShaderObject::AttachNativeShader(ShaderType type, RenderEffect const& effect,
		std::array<uint32_t, ST_NumShaderTypes> const& shader_desc_ids, std::vector<uint8_t> const& native_shader_block)
	{
		bool ret = false;

		this->CreateShaderStage(type);
		auto* shader_stage = so_template_->shader_stages_[type].get();
		shader_stage->StreamIn(effect, shader_desc_ids, native_shader_block);
		is_shader_validate_[type] = shader_stage->Validate();
		if (is_shader_validate_[type])
		{
			this->AppendTexSamplerBinds(type, effect, shader_stage->TexSamplerPairs());
			this->FillTFBVaryings(effect.GetShaderDesc(shader_desc_ids[type]));

			ret = is_shader_validate_[type];
		}

		return ret;
	}

	bool OGLShaderObject::StreamIn(ResIdentifierPtr const& res, ShaderType type, RenderEffect const& effect,
		std::array<uint32_t, ST_NumShaderTypes> const& shader_desc_ids)
	{
		uint32_t len;
		res->read(&len, sizeof(len));
		len = LE2Native(len);
		std::vector<uint8_t> native_shader_block(len);
		if (len > 0)
		{
			res->read(&native_shader_block[0], len * sizeof(native_shader_block[0]));
		}

		return this->AttachNativeShader(type, effect, shader_desc_ids, native_shader_block);
	}

	void OGLShaderObject::StreamOut(std::ostream& os, ShaderType type)
	{
		so_template_->shader_stages_[type]->StreamOut(os);
	}

	void OGLShaderObject::AttachShader(ShaderType type, RenderEffect const& effect, RenderTechnique const& tech, RenderPass const& pass,
		std::array<uint32_t, ST_NumShaderTypes> const& shader_desc_ids)
	{
		this->CreateShaderStage(type);

		auto* shader_stage = so_template_->shader_stages_[type].get();
		if (type == ST_DomainShader)
		{
			auto* hull_shader_stage = checked_cast<OGLHullShaderStageObject*>(so_template_->shader_stages_[ST_HullShader].get());
			checked_cast<OGLDomainShaderStageObject*>(shader_stage)
				->DsParameters(hull_shader_stage->DsPartitioning(), hull_shader_stage->DsOutputPrimitive());
		}
		shader_stage->AttachShader(effect, tech, pass, shader_desc_ids);
		is_shader_validate_[type] = shader_stage->Validate();

		if (is_shader_validate_[type])
		{
			this->AppendTexSamplerBinds(type, effect, shader_stage->TexSamplerPairs());
			this->FillTFBVaryings(effect.GetShaderDesc(shader_desc_ids[type]));
		}
	}

	void OGLShaderObject::AttachShader(ShaderType type, RenderEffect const& effect, RenderTechnique const& /*tech*/,
		RenderPass const& /*pass*/, ShaderObjectPtr const& shared_so)
	{
		auto so = checked_cast<OGLShaderObject*>(shared_so.get());

		is_shader_validate_[type] = so->is_shader_validate_[type];
		so_template_->shader_stages_[type] = so->so_template_->shader_stages_[type];

		if (is_shader_validate_[type])
		{
			switch (type)
			{
			case ST_VertexShader:
			case ST_GeometryShader:
			case ST_DomainShader:
				so_template_->glsl_tfb_varyings_ = so->so_template_->glsl_tfb_varyings_;
				break;
			
			case ST_PixelShader:
				has_discard_ = so->has_discard_;
				break;

			default:
				break;
			}

			this->AppendTexSamplerBinds(type, effect, so_template_->shader_stages_[type]->TexSamplerPairs());
		}
	}

	void OGLShaderObject::LinkShaders(RenderEffect const & effect)
	{
		is_validate_ = true;
		for (size_t type = 0; type < ShaderObject::ST_NumShaderTypes; ++ type)
		{
			if (so_template_->shader_stages_[type] && !so_template_->shader_stages_[type]->ShaderFuncName().empty())
			{
				is_validate_ &= is_shader_validate_[type];
			}
		}

		if (is_validate_)
		{
			glProgramParameteri(glsl_program_, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);

			this->LinkGLSL();
			this->AttachUBOs(effect);

			if (is_validate_)
			{
				GLint num = 0;
				glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &num);
				if (num > 0)
				{
					GLint len = 0;
					glGetProgramiv(glsl_program_, GL_PROGRAM_BINARY_LENGTH, &len);
					so_template_->glsl_bin_program_.resize(len);
					glGetProgramBinary(glsl_program_, len, nullptr, &so_template_->glsl_bin_format_,
						so_template_->glsl_bin_program_.data());
				}
			}

			for (int type = 0; type < ST_NumShaderTypes; ++ type)
			{
				auto const* shader_stage = so_template_->shader_stages_[type].get();
				if (shader_stage)
				{
					for (size_t pi = 0; pi < shader_stage->PNames().size(); ++pi)
					{
						GLint location = glGetUniformLocation(glsl_program_, shader_stage->GlslResNames()[pi].c_str());
						if (location != -1)
						{
							RenderEffectParameter* p = effect.ParameterByName(shader_stage->PNames()[pi]);
							if (p)
							{
								BOOST_ASSERT(REDT_buffer == p->Type());

								ParameterBind pb;
								pb.param = p;
								pb.location = location;

								uint32_t index = static_cast<uint32_t>(textures_.size());
								textures_.resize(index + 1);
								gl_bind_targets_.resize(index + 1);
								gl_bind_textures_.resize(index + 1);
								gl_bind_samplers_.resize(index + 1);

								pb.func = SetOGLShaderParameter<GraphicsBufferPtr>(textures_,
									gl_bind_targets_, gl_bind_textures_, gl_bind_samplers_, location, index, p);

								param_binds_.push_back(pb);
							}
							else
							{
								for (size_t i = 0; i < tex_sampler_binds_.size(); ++ i)
								{
									if (std::get<0>(tex_sampler_binds_[i]) == shader_stage->PNames()[pi])
									{
										ParameterBind pb;
										pb.combined_sampler_name = std::get<0>(tex_sampler_binds_[i]);
										pb.param = nullptr;
										pb.location = location;
										pb.tex_sampler_bind_index = static_cast<int>(i);

										uint32_t index = static_cast<uint32_t>(textures_.size());
										textures_.resize(index + 1);
										gl_bind_targets_.resize(index + 1);
										gl_bind_textures_.resize(index + 1);
										gl_bind_samplers_.resize(index + 1);

										pb.func = SetOGLShaderParameter<TexturePtr>(textures_,
											gl_bind_targets_, gl_bind_textures_, gl_bind_samplers_,
											location, index, std::get<1>(tex_sampler_binds_[i]), std::get<2>(tex_sampler_binds_[i]));

										param_binds_.push_back(pb);

										break;
									}
								}
							}
						}
					}
				}
			}

			{
				auto const* vs_shader_stage =
					checked_cast<OGLVertexShaderStageObject const*>(so_template_->shader_stages_[ST_VertexShader].get());
				for (size_t pi = 0; pi < vs_shader_stage->GlslAttribNames().size(); ++pi)
				{
					attrib_locs_.emplace(std::make_pair(vs_shader_stage->Usages()[pi], vs_shader_stage->UsageIndices()[pi]),
						glGetAttribLocation(glsl_program_, vs_shader_stage->GlslAttribNames()[pi].c_str()));
				}
			}
		}
	}

	ShaderObjectPtr OGLShaderObject::Clone(RenderEffect const & effect)
	{
		OGLShaderObjectPtr ret = MakeSharedPtr<OGLShaderObject>(so_template_);

		ret->is_shader_validate_ = is_shader_validate_;
		ret->is_validate_ = is_validate_;
		ret->has_discard_ = has_discard_;
		ret->has_tessellation_ = has_tessellation_;

		ret->tex_sampler_binds_.resize(tex_sampler_binds_.size());
		for (size_t i = 0; i < tex_sampler_binds_.size(); ++ i)
		{
			std::get<0>(ret->tex_sampler_binds_[i]) = std::get<0>(tex_sampler_binds_[i]);
			std::get<1>(ret->tex_sampler_binds_[i]) = effect.ParameterByName(std::get<1>(tex_sampler_binds_[i])->Name());
			std::get<2>(ret->tex_sampler_binds_[i]) = effect.ParameterByName(std::get<2>(tex_sampler_binds_[i])->Name());
			std::get<3>(ret->tex_sampler_binds_[i]) = std::get<3>(tex_sampler_binds_[i]);
		}

		if (ret->is_validate_)
		{
			if (!so_template_->glsl_bin_program_.empty())
			{
				glProgramParameteri(ret->glsl_program_, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
				glProgramBinary(ret->glsl_program_, so_template_->glsl_bin_format_,
					so_template_->glsl_bin_program_.data(), static_cast<GLsizei>(so_template_->glsl_bin_program_.size()));

#ifdef KLAYGE_DEBUG
				GLint linked = false;
				glGetProgramiv(ret->glsl_program_, GL_LINK_STATUS, &linked);
				if (!linked)
				{
					GLint len = 0;
					glGetProgramiv(ret->glsl_program_, GL_INFO_LOG_LENGTH, &len);
					if (len > 0)
					{
						std::vector<char> info(len);
						glGetProgramInfoLog(ret->glsl_program_, len, &len, &info[0]);
						LogError() << std::string_view(info.data(), len) << std::endl;
					}
				}
#endif
			}
			else
			{
				ret->LinkGLSL();
			}

			ret->AttachUBOs(effect);
			ret->attrib_locs_ = attrib_locs_;
			for (auto const & pb : param_binds_)
			{
				if (pb.param)
				{
					RenderEffectParameter* p = effect.ParameterByName(pb.param->Name());
					BOOST_ASSERT(REDT_buffer == p->Type());

					ParameterBind new_pb;
					new_pb.param = p;
					new_pb.location = pb.location;

					uint32_t index = static_cast<uint32_t>(ret->textures_.size());
					ret->textures_.resize(index + 1);
					ret->gl_bind_targets_.resize(index + 1);
					ret->gl_bind_textures_.resize(index + 1);
					ret->gl_bind_samplers_.resize(index + 1);

					new_pb.func = SetOGLShaderParameter<GraphicsBufferPtr>(ret->textures_,
						ret->gl_bind_targets_, ret->gl_bind_textures_, ret->gl_bind_samplers_,
						new_pb.location, index, p);

					ret->param_binds_.push_back(new_pb);
				}
				else
				{
					std::string const & pname = pb.combined_sampler_name;
					for (size_t j = 0; j < ret->tex_sampler_binds_.size(); ++ j)
					{
						if (std::get<0>(ret->tex_sampler_binds_[j]) == pname)
						{
							ParameterBind new_pb;
							new_pb.combined_sampler_name = pname;
							new_pb.param = nullptr;
							new_pb.location = pb.location;
							new_pb.tex_sampler_bind_index = pb.tex_sampler_bind_index;

							uint32_t index = static_cast<uint32_t>(ret->textures_.size());
							ret->textures_.resize(index + 1);
							ret->gl_bind_targets_.resize(index + 1);
							ret->gl_bind_textures_.resize(index + 1);
							ret->gl_bind_samplers_.resize(index + 1);

							new_pb.func = SetOGLShaderParameter<TexturePtr>(ret->textures_,
								ret->gl_bind_targets_, ret->gl_bind_textures_, ret->gl_bind_samplers_,
								new_pb.location, index,
								std::get<1>(ret->tex_sampler_binds_[new_pb.tex_sampler_bind_index]),
								std::get<2>(ret->tex_sampler_binds_[new_pb.tex_sampler_bind_index]));

							ret->param_binds_.push_back(new_pb);

							break;
						}
					}
				}
			}
		}

		return ret;
	}

	GLint OGLShaderObject::GetAttribLocation(VertexElementUsage usage, uint8_t usage_index)
	{
		auto iter = attrib_locs_.find(std::make_pair(usage, usage_index));
		if (iter != attrib_locs_.end())
		{
			return iter->second;
		}
		else
		{
			return -1;
		}
	}

	void OGLShaderObject::CreateShaderStage(ShaderType type)
	{
		std::shared_ptr<OGLShaderStageObject> shader_stage;
		switch (type)
		{
		case ShaderObject::ST_VertexShader:
			shader_stage = MakeSharedPtr<OGLVertexShaderStageObject>();
			break;

		case ShaderObject::ST_PixelShader:
			shader_stage = MakeSharedPtr<OGLPixelShaderStageObject>();
			break;

		case ShaderObject::ST_GeometryShader:
			shader_stage = MakeSharedPtr<OGLGeometryShaderStageObject>();
			break;

		case ShaderObject::ST_ComputeShader:
			shader_stage = MakeSharedPtr<OGLComputeShaderStageObject>();
			break;

		case ShaderObject::ST_HullShader:
			shader_stage = MakeSharedPtr<OGLHullShaderStageObject>();
			break;

		case ShaderObject::ST_DomainShader:
			shader_stage = MakeSharedPtr<OGLDomainShaderStageObject>();
			break;

		default:
			KFL_UNREACHABLE("Invalid shader stage");
		}
		so_template_->shader_stages_[type] = shader_stage;
	}

	void OGLShaderObject::AppendTexSamplerBinds(
		ShaderType stage, RenderEffect const& effect, std::vector<std::pair<std::string, std::string>> const& tex_sampler_pairs)
	{
		uint32_t const mask = 1UL << stage;
		for (auto const& tex_sampler : tex_sampler_pairs)
		{
			std::string const combined_sampler_name = tex_sampler.first + "_" + tex_sampler.second;

			bool found = false;
			for (uint32_t k = 0; k < tex_sampler_binds_.size(); ++k)
			{
				if (std::get<0>(tex_sampler_binds_[k]) == combined_sampler_name)
				{
					std::get<3>(tex_sampler_binds_[k]) |= mask;
					found = true;
					break;
				}
			}
			if (!found)
			{
				tex_sampler_binds_.push_back(std::make_tuple(combined_sampler_name, effect.ParameterByName(tex_sampler.first),
					effect.ParameterByName(tex_sampler.second), mask));
			}
		}
	}

	void OGLShaderObject::LinkGLSL()
	{
		for (size_t type = 0; type < ST_NumShaderTypes; ++type)
		{
			if (is_shader_validate_[type])
			{
				auto const* shader_stage = so_template_->shader_stages_[type].get();
				if (shader_stage && !shader_stage->ShaderFuncName().empty())
				{
					BOOST_ASSERT(shader_stage->GlShader() != 0);
					glAttachShader(glsl_program_, shader_stage->GlShader());
				}
			}
		}

		if (!so_template_->glsl_tfb_varyings_.empty())
		{
			std::vector<GLchar const *> names(so_template_->glsl_tfb_varyings_.size());
			for (size_t i = 0; i < so_template_->glsl_tfb_varyings_.size(); ++ i)
			{
				names[i] = so_template_->glsl_tfb_varyings_[i].c_str();
			}

			glTransformFeedbackVaryings(glsl_program_, static_cast<GLsizei>(so_template_->glsl_tfb_varyings_.size()),
				&names[0], so_template_->tfb_separate_attribs_ ? GL_SEPARATE_ATTRIBS : GL_INTERLEAVED_ATTRIBS);
		}

		glLinkProgram(glsl_program_);

		GLint linked = false;
		glGetProgramiv(glsl_program_, GL_LINK_STATUS, &linked);
#ifdef KLAYGE_DEBUG
		if (!linked)
		{
			std::string shader_names;
			for (size_t type = 0; type < ShaderObject::ST_NumShaderTypes; ++ type)
			{
				std::string const& func_name = so_template_->shader_stages_[type]->ShaderFuncName();
				if (!func_name.empty())
				{
					shader_names += func_name + '/';
				}
			}
			if (!shader_names.empty())
			{
				shader_names.resize(shader_names.size() - 1);
			}

			LogError() << "Error when linking GLSLs " << shader_names << ":" << std::endl;

			GLint len = 0;
			glGetProgramiv(glsl_program_, GL_INFO_LOG_LENGTH, &len);
			if (len > 0)
			{
				std::vector<char> info(len);
				glGetProgramInfoLog(glsl_program_, len, &len, &info[0]);

				OGLRenderEngine& re = *checked_cast<OGLRenderEngine*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
				if (re.HackForNV())
				{
					MemInputStreamBuf info_buff(info.data(), info.size());
					std::istream err_iss(&info_buff);
					std::string type1, type2;
					err_iss >> type1 >> type2;

					ShaderType shader_type;
					if ("Vertex" == type1)
					{
						shader_type = ST_VertexShader;
					}
					else if ("Geometry" == type1)
					{
						shader_type = ST_GeometryShader;
					}
					else if ("Fragment" == type1)
					{
						shader_type = ST_PixelShader;
					}
					else if ("Tessellation" == type1)
					{
						if ("control" == type2)
						{
							shader_type = ST_HullShader;
						}
						else
						{
							shader_type = ST_DomainShader;
						}
					}
					else
					{
						shader_type = ST_NumShaderTypes;
					}

					if (shader_type != ST_NumShaderTypes)
					{
						PrintGlslError(so_template_->shader_stages_[shader_type]->GlslSource(),
							std::string_view(info.data(), info.size()));
					}
					else
					{
						LogError() << std::string_view(info.data(), len) << std::endl;
					}
				}
				else
				{
					LogError() << std::string_view(info.data(), len) << std::endl;
				}
			}
		}
#endif
		is_validate_ &= linked ? true : false;
	}

	void OGLShaderObject::AttachUBOs(RenderEffect const & effect)
	{
		GLint active_ubos = 0;
		glGetProgramiv(glsl_program_, GL_ACTIVE_UNIFORM_BLOCKS, &active_ubos);
		all_cbuffs_.resize(active_ubos);
		gl_bind_cbuffs_.resize(active_ubos);
		for (int i = 0; i < active_ubos; ++ i)
		{
			GLint length = 0;
			glGetActiveUniformBlockiv(glsl_program_, i, GL_UNIFORM_BLOCK_NAME_LENGTH, &length);

			std::vector<GLchar> ubo_name(length, '\0');
			glGetActiveUniformBlockName(glsl_program_, i, length, nullptr, &ubo_name[0]);

			auto cbuff = effect.CBufferByName(&ubo_name[0]);
			BOOST_ASSERT(cbuff);
			all_cbuffs_[i] = cbuff;

			glUniformBlockBinding(glsl_program_, glGetUniformBlockIndex(glsl_program_, &ubo_name[0]), i);

			GLint ubo_size = 0;
			glGetActiveUniformBlockiv(glsl_program_, i, GL_UNIFORM_BLOCK_DATA_SIZE, &ubo_size);
			cbuff->Resize(ubo_size);
			gl_bind_cbuffs_[i] = checked_cast<OGLGraphicsBuffer*>(cbuff->HWBuff().get())->GLvbo();

			GLint uniforms = 0;
			glGetActiveUniformBlockiv(glsl_program_, i, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &uniforms);

			std::vector<GLuint> uniform_indices(uniforms);
			glGetActiveUniformBlockiv(glsl_program_, i, GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES,
				reinterpret_cast<GLint*>(&uniform_indices[0]));

			std::vector<GLint> uniform_name_lens(uniforms);
			glGetActiveUniformsiv(glsl_program_, uniforms, &uniform_indices[0],
				GL_UNIFORM_NAME_LENGTH, &uniform_name_lens[0]);

			std::vector<GLint> uniform_array_strides(uniforms);
			glGetActiveUniformsiv(glsl_program_, uniforms, &uniform_indices[0],
				GL_UNIFORM_ARRAY_STRIDE, &uniform_array_strides[0]);

			std::vector<GLint> uniform_matrix_strides(uniforms);
			glGetActiveUniformsiv(glsl_program_, uniforms, &uniform_indices[0],
				GL_UNIFORM_MATRIX_STRIDE, &uniform_matrix_strides[0]);

			std::vector<GLint> uniform_offsets(uniforms);
			glGetActiveUniformsiv(glsl_program_, uniforms, &uniform_indices[0],
				GL_UNIFORM_OFFSET, &uniform_offsets[0]);

			for (GLint j = 0; j < uniforms; ++ j)
			{
				std::vector<GLchar> uniform_name(uniform_name_lens[j], '\0');
				GLint size;
				GLenum type;
				glGetActiveUniform(glsl_program_, uniform_indices[j], uniform_name_lens[j],
					nullptr, &size, &type, &uniform_name[0]);

				auto iter = std::find(uniform_name.begin(), uniform_name.end(), '[');
				if (iter != uniform_name.end())
				{
					*iter = '\0';
				}

				RenderEffectParameter* param = effect.ParameterByName(&uniform_name[0]);
				GLint stride;
				if (param->ArraySize())
				{
					stride = uniform_array_strides[j];
				}
				else
				{
					if (param->Type() != REDT_float4x4)
					{
						stride = 4;
					}
					else
					{
						stride = uniform_matrix_strides[j];
					}
				}
				param->BindToCBuffer(*cbuff, uniform_offsets[j], stride);
			}
		}
	}

	void OGLShaderObject::FillTFBVaryings(ShaderDesc const & sd)
	{
		int slot = -1;
		so_template_->tfb_separate_attribs_ = false;
		for (auto const & decl : sd.so_decl)
		{
			if (slot < 0)
			{
				slot = decl.slot;
			}
			else
			{
				if (slot != decl.slot)
				{
					so_template_->tfb_separate_attribs_ = true;
				}
			}

			std::string glsl_param_name;
			switch (decl.usage)
			{
			case VEU_Position:
				glsl_param_name = "gl_Position";
				break;

			case VEU_Normal:
				glsl_param_name = "v_NORMAL0";
				break;

			case VEU_Diffuse:
				glsl_param_name = "v_COLOR0";
				break;

			case VEU_Specular:
				glsl_param_name = "v_COLOR1";
				break;

			case VEU_BlendWeight:
				glsl_param_name = "v_BLENDWEIGHT0";
				break;

			case VEU_BlendIndex:
				glsl_param_name = "v_BLENDINDICES0";
				break;

			case VEU_TextureCoord:
				glsl_param_name = "v_TEXCOORD" + std::to_string(static_cast<int>(decl.usage_index));
				break;

			case VEU_Tangent:
				glsl_param_name = "v_TANGENT0";
				break;

			case VEU_Binormal:
				glsl_param_name = "v_BINORMAL0";
				break;

			default:
				KFL_UNREACHABLE("Invalid usage");
			}

			so_template_->glsl_tfb_varyings_.push_back(glsl_param_name);
		}
	}

	void OGLShaderObject::Bind()
	{
		if (!so_template_->shader_stages_[ShaderObject::ST_PixelShader] ||
			so_template_->shader_stages_[ShaderObject::ST_PixelShader]->GlslSource().empty())
		{
			glEnable(GL_RASTERIZER_DISCARD);
		}

		OGLRenderEngine& re = *checked_cast<OGLRenderEngine*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		re.UseProgram(glsl_program_);

		for (auto const & pb : param_binds_)
		{
			pb.func();
		}

		for (size_t i = 0; i < all_cbuffs_.size(); ++ i)
		{
			all_cbuffs_[i]->Update();
		}

		if (!gl_bind_cbuffs_.empty())
		{
			re.BindBuffersBase(GL_UNIFORM_BUFFER, 0, static_cast<GLsizei>(all_cbuffs_.size()), &gl_bind_cbuffs_[0]);
		}

		if (!gl_bind_textures_.empty())
		{
			re.BindTextures(0, static_cast<GLsizei>(gl_bind_textures_.size()), &gl_bind_targets_[0], &gl_bind_textures_[0]);
		}

		if (!gl_bind_samplers_.empty())
		{
			re.BindSamplers(0, static_cast<GLsizei>(gl_bind_samplers_.size()), &gl_bind_samplers_[0]);
		}

#ifdef KLAYGE_DEBUG
		glValidateProgram(glsl_program_);

		GLint validated = false;
		glGetProgramiv(glsl_program_, GL_VALIDATE_STATUS, &validated);
		if (!validated)
		{
			GLint len = 0;
			glGetProgramiv(glsl_program_, GL_INFO_LOG_LENGTH, &len);
			if (len > 0)
			{
				std::vector<char> info(len);
				glGetProgramInfoLog(glsl_program_, len, &len, &info[0]);
				LogError() << std::string_view(info.data(), len) << std::endl;
			}
		}
#endif
	}

	void OGLShaderObject::Unbind()
	{
		if (!so_template_->shader_stages_[ShaderObject::ST_PixelShader] ||
			so_template_->shader_stages_[ShaderObject::ST_PixelShader]->GlslSource().empty())
		{
			glDisable(GL_RASTERIZER_DISCARD);
		}
	}
}
