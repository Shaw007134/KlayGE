// OGLESShaderObject.hpp
// KlayGE OpenGL ES shader对象类 头文件
// Ver 3.10.0
// 版权所有(C) 龚敏敏, 2010
// Homepage: http://www.klayge.org
//
// 3.10.0
// 初次建立 (2010.1.22)
//
// 修改记录
//////////////////////////////////////////////////////////////////////////////////

#ifndef _OGLESSHADEROBJECT_HPP
#define _OGLESSHADEROBJECT_HPP

#pragma once

#include <KlayGE/PreDeclare.hpp>
#include <KlayGE/RenderLayout.hpp>
#include <KlayGE/ShaderObject.hpp>

namespace DXBC2GLSL
{
	class DXBC2GLSL;
}

namespace KlayGE
{
	class OGLESShaderStageObject
	{
	public:
		OGLESShaderStageObject(ShaderObject::ShaderType stage, GLenum gl_shader_type);
		virtual ~OGLESShaderStageObject();

		void StreamIn(RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids,
			std::vector<uint8_t> const& native_shader_block);
		void StreamOut(std::ostream& os);
		void AttachShader(RenderEffect const& effect, RenderTechnique const& tech, RenderPass const& pass,
			std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids);

		bool Validate() const
		{
			return is_validate_;
		}

		std::string const& GlslSource() const
		{
			return glsl_src_;
		}

		std::string const& ShaderFuncName() const
		{
			return shader_func_name_;
		}

		std::vector<std::string> const& PNames() const
		{
			return pnames_;
		}

		std::vector<std::string> const& GlslResNames() const
		{
			return glsl_res_names_;
		}

		std::vector<std::pair<std::string, std::string>> const& TexSamplerPairs() const
		{
			return tex_sampler_pairs_;
		}

		GLuint GlShader() const
		{
			return gl_shader_;
		}

		virtual uint32_t DsPartitioning() const
		{
			return 0;
		}
		virtual uint32_t DsOutputPrimitive() const
		{
			return 0;
		}

	private:
		std::string_view GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const;
		void CreateHwShader();

		virtual void StageSpecificStreamIn(std::istream& native_shader_stream)
		{
			KFL_UNUSED(native_shader_stream);
		}
		virtual void StageSpecificStreamOut(std::ostream& os)
		{
			KFL_UNUSED(os);
		}
#if KLAYGE_IS_DEV_PLATFORM
		virtual void StageSpecificAttachShader(DXBC2GLSL::DXBC2GLSL const& dxbc2glsl)
		{
			KFL_UNUSED(dxbc2glsl);
		}
#endif

	protected:
		const ShaderObject::ShaderType stage_;
		const GLenum gl_shader_type_;

		bool is_available_;
		bool is_validate_;

		std::string shader_func_name_;
		std::string glsl_src_;
		std::vector<std::string> pnames_;
		std::vector<std::string> glsl_res_names_;

		std::vector<std::pair<std::string, std::string>> tex_sampler_pairs_;

		GLuint gl_shader_ = 0;
	};

	class OGLESVertexShaderStageObject : public OGLESShaderStageObject
	{
	public:
		OGLESVertexShaderStageObject();

		std::vector<VertexElementUsage> const& Usages() const
		{
			return usages_;
		}
		std::vector<uint8_t> const& UsageIndices() const
		{
			return usage_indices_;
		}
		std::vector<std::string> const& GlslAttribNames() const
		{
			return glsl_attrib_names_;
		}

	private:
		void StageSpecificStreamIn(std::istream& native_shader_stream) override;
		void StageSpecificStreamOut(std::ostream& os) override;
#if KLAYGE_IS_DEV_PLATFORM
		void StageSpecificAttachShader(DXBC2GLSL::DXBC2GLSL const& dxbc2glsl) override;
#endif

	private:
		std::vector<VertexElementUsage> usages_;
		std::vector<uint8_t> usage_indices_;
		std::vector<std::string> glsl_attrib_names_;
	};

	class OGLESPixelShaderStageObject : public OGLESShaderStageObject
	{
	public:
		OGLESPixelShaderStageObject();
	};

	class OGLESGeometryShaderStageObject : public OGLESShaderStageObject
	{
	public:
		OGLESGeometryShaderStageObject();
	};

	class OGLESComputeShaderStageObject : public OGLESShaderStageObject
	{
	public:
		OGLESComputeShaderStageObject();
	};

	class OGLESHullShaderStageObject : public OGLESShaderStageObject
	{
	public:
		OGLESHullShaderStageObject();

#if KLAYGE_IS_DEV_PLATFORM
		uint32_t DsPartitioning() const override
		{
			return ds_partitioning_;
		}
		uint32_t DsOutputPrimitive() const override
		{
			return ds_output_primitive_;
		}
#endif

	private:
#if KLAYGE_IS_DEV_PLATFORM
		void StageSpecificAttachShader(DXBC2GLSL::DXBC2GLSL const& dxbc2glsl) override;
#endif

#if KLAYGE_IS_DEV_PLATFORM
	private:
		uint32_t ds_partitioning_ = 0;
		uint32_t ds_output_primitive_ = 0;
#endif
	};

	class OGLESDomainShaderStageObject : public OGLESShaderStageObject
	{
	public:
		OGLESDomainShaderStageObject();

#if KLAYGE_IS_DEV_PLATFORM
		void DsParameters(uint32_t partitioning, uint32_t output_primitive);
#endif

	private:
#if KLAYGE_IS_DEV_PLATFORM
	private:
		uint32_t ds_partitioning_ = 0;
		uint32_t ds_output_primitive_ = 0;
#endif
	};

	struct TextureBind
	{
		ShaderResourceViewPtr buff_srv;

		ShaderResourceViewPtr tex_srv;
		SamplerStateObjectPtr sampler;
	};

	class OGLESShaderObject : public ShaderObject
	{
	public:
		OGLESShaderObject();
		~OGLESShaderObject();

		bool AttachNativeShader(ShaderType type, RenderEffect const & effect,
			std::array<uint32_t, ST_NumShaderTypes> const & shader_desc_ids, std::vector<uint8_t> const & native_shader_block) override;

		bool StreamIn(ResIdentifierPtr const & res, ShaderType type, RenderEffect const & effect,
			std::array<uint32_t, ST_NumShaderTypes> const & shader_desc_ids) override;
		void StreamOut(std::ostream& os, ShaderType type) override;

		void AttachShader(ShaderType type, RenderEffect const & effect,
			RenderTechnique const & tech, RenderPass const & pass,
			std::array<uint32_t, ST_NumShaderTypes> const & shader_desc_ids) override;
		void AttachShader(ShaderType type, RenderEffect const & effect,
			RenderTechnique const & tech, RenderPass const & pass, ShaderObjectPtr const & shared_so) override;
		void LinkShaders(RenderEffect const & effect) override;
		ShaderObjectPtr Clone(RenderEffect const & effect) override;

		void Bind();
		void Unbind();

		GLint GetAttribLocation(VertexElementUsage usage, uint8_t usage_index);

		GLuint GLSLProgram() const
		{
			return glsl_program_;
		}

	private:
		struct OGLESShaderObjectTemplate
		{
			GLenum glsl_bin_format_;
			std::vector<uint8_t> glsl_bin_program_;
			std::array<std::shared_ptr<OGLESShaderStageObject>, ST_NumShaderTypes> shader_stages_;
			std::vector<std::string> glsl_tfb_varyings_;
			bool tfb_separate_attribs_;
		};

		struct ParameterBind
		{
			std::string combined_sampler_name;
			RenderEffectParameter* param;
			int location;
			int tex_sampler_bind_index;
			std::function<void()> func;
		};

	public:
		explicit OGLESShaderObject(std::shared_ptr<OGLESShaderObjectTemplate> const & so_template);

	private:
		void CreateShaderStage(ShaderType stage);
		void AppendTexSamplerBinds(
			ShaderType stage, RenderEffect const& effect, std::vector<std::pair<std::string, std::string>> const& tex_sampler_pairs);
		void LinkGLSL();
		void AttachUBOs(RenderEffect const & effect);
		void FillTFBVaryings(ShaderDesc const & sd);

	private:
		std::shared_ptr<OGLESShaderObjectTemplate> so_template_;

		GLuint glsl_program_;

		std::vector<ParameterBind> param_binds_;

		std::vector<TextureBind> textures_;
		std::vector<GLuint> gl_bind_targets_;
		std::vector<GLuint> gl_bind_textures_;
		std::vector<GLuint> gl_bind_samplers_;
		std::vector<GLuint> gl_bind_cbuffs_;

		std::vector<std::tuple<std::string, RenderEffectParameter*, RenderEffectParameter*, uint32_t>> tex_sampler_binds_;

		std::map<std::pair<VertexElementUsage, uint8_t>, GLint> attrib_locs_;

		std::vector<RenderEffectConstantBuffer*> all_cbuffs_;
	};

	typedef std::shared_ptr<OGLESShaderObject> OGLESShaderObjectPtr;
}

#endif			// _OGLESSHADEROBJECT_HPP
