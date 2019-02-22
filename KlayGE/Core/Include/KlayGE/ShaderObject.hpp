// ShaderObject.hpp
// KlayGE shader对象类 头文件
// Ver 3.8.0
// 版权所有(C) 龚敏敏, 2006-2009
// Homepage: http://www.klayge.org
//
// 3.8.0
// 支持Gemoetry Shader (2009.2.5)
//
// 3.7.0
// 改为直接传入RenderEffect (2008.7.4)
//
// 3.5.0
// 初次建立 (2006.11.2)
//
// 修改记录
//////////////////////////////////////////////////////////////////////////////////

#ifndef _SHADEROBJECT_HPP
#define _SHADEROBJECT_HPP

#pragma once

#include <KlayGE/PreDeclare.hpp>
#include <KlayGE/RenderLayout.hpp>

#include <array>

namespace KlayGE
{
	struct ShaderDesc
	{
		ShaderDesc()
			: macros_hash(0), tech_pass_type(0xFFFFFFFF)
		{
		}

		std::string profile;
		std::string func_name;
		uint64_t macros_hash;

		uint32_t tech_pass_type;

#ifdef KLAYGE_HAS_STRUCT_PACK
		#pragma pack(push, 1)
#endif
		struct StreamOutputDecl
		{
			VertexElementUsage usage;
			uint8_t usage_index;
			uint8_t start_component;
			uint8_t component_count;
			uint8_t slot;

			friend bool operator==(StreamOutputDecl const & lhs, StreamOutputDecl const & rhs)
			{
				return (lhs.usage == rhs.usage) && (lhs.usage_index == rhs.usage_index)
					&& (lhs.start_component == rhs.start_component) && (lhs.component_count == rhs.component_count)
					&& (lhs.slot == rhs.slot);
			}
			friend bool operator!=(StreamOutputDecl const & lhs, StreamOutputDecl const & rhs)
			{
				return !(lhs == rhs);
			}
		};
#ifdef KLAYGE_HAS_STRUCT_PACK
		#pragma pack(pop)
#endif
		std::vector<StreamOutputDecl> so_decl;

		friend bool operator==(ShaderDesc const & lhs, ShaderDesc const & rhs)
		{
			return (lhs.profile == rhs.profile) && (lhs.func_name == rhs.func_name)
				&& (lhs.macros_hash == rhs.macros_hash) && (lhs.so_decl == rhs.so_decl);
		}
		friend bool operator!=(ShaderDesc const & lhs, ShaderDesc const & rhs)
		{
			return !(lhs == rhs);
		}
	};

	enum class ShaderStage
	{
		VertexShader,
		PixelShader,
		GeometryShader,
		ComputeShader,
		HullShader,
		DomainShader,

		Num,
	};
	uint32_t constexpr NumShaderStages = static_cast<uint32_t>(ShaderStage::Num);

	class KLAYGE_CORE_API ShaderStageObject : boost::noncopyable
	{
	public:
		explicit ShaderStageObject(ShaderStage stage);
		virtual ~ShaderStageObject();

		virtual void StreamIn(RenderEffect const& effect, std::array<uint32_t, NumShaderStages> const& shader_desc_ids,
			std::vector<uint8_t> const& native_shader_block) = 0;
		virtual void StreamOut(std::ostream& os) = 0;
		virtual void AttachShader(RenderEffect const& effect, RenderTechnique const& tech, RenderPass const& pass,
			std::array<uint32_t, NumShaderStages> const& shader_desc_ids) = 0;

		bool Validate() const
		{
			return is_validate_;
		}

		// Compute shader only
		virtual uint32_t BlockSizeX() const
		{
			return 0;
		}
		virtual uint32_t BlockSizeY() const
		{
			return 0;
		}
		virtual uint32_t BlockSizeZ() const
		{
			return 0;
		}

	protected:
		virtual std::string_view GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const = 0;
		virtual void CreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, NumShaderStages> const& shader_desc_ids) = 0;

		virtual void StageSpecificStreamIn(std::istream& native_shader_stream)
		{
			KFL_UNUSED(native_shader_stream);
		}
		virtual void StageSpecificStreamOut(std::ostream& os)
		{
			KFL_UNUSED(os);
		}
		virtual void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, NumShaderStages> const& shader_desc_ids)
		{
			KFL_UNUSED(effect);
			KFL_UNUSED(shader_desc_ids);
		}

	protected:
		const ShaderStage stage_;

		bool is_validate_;
	};

	class KLAYGE_CORE_API ShaderObject : boost::noncopyable
	{
	public:
		ShaderObject();
		virtual ~ShaderObject();
		
		void AttachStage(ShaderStage stage, ShaderStageObjectPtr const& shader_stage)
		{
			so_template_->shader_stages_[static_cast<uint32_t>(stage)] = shader_stage;
		}
		ShaderStageObjectPtr const& Stage(ShaderStage stage) const
		{
			return so_template_->shader_stages_[static_cast<uint32_t>(stage)];
		}

		virtual bool AttachNativeShader(ShaderStage stage, RenderEffect const& effect,
			std::array<uint32_t, NumShaderStages> const& shader_desc_ids,
			std::vector<uint8_t> const& native_shader_block) = 0;

		bool StreamIn(ResIdentifierPtr const & res, ShaderStage stage, RenderEffect const & effect,
			std::array<uint32_t, NumShaderStages> const & shader_desc_ids);
		void StreamOut(std::ostream& os, ShaderStage stage);

		virtual void AttachShader(ShaderStage stage, RenderEffect const& effect, RenderTechnique const& tech,
			RenderPass const& pass, std::array<uint32_t, NumShaderStages> const& shader_desc_ids) = 0;
		virtual void AttachShader(ShaderStage stage, RenderEffect const & effect,
			RenderTechnique const & tech, RenderPass const & pass, ShaderObjectPtr const & shared_so) = 0;
		virtual void LinkShaders(RenderEffect const & effect) = 0;
		virtual ShaderObjectPtr Clone(RenderEffect const & effect) = 0;

		virtual void Bind() = 0;
		virtual void Unbind() = 0;

		bool Validate() const
		{
			return is_validate_;
		}

		bool HasDiscard() const
		{
			return has_discard_;
		}

	protected:
		struct ShaderObjectTemplate
		{
			std::array<std::shared_ptr<ShaderStageObject>, NumShaderStages> shader_stages_;
		};

	public:
		ShaderObject(std::shared_ptr<ShaderObjectTemplate> const& so_template);

		static std::vector<uint8_t> CompileToDXBC(ShaderStage stage, RenderEffect const & effect,
			RenderTechnique const & tech, RenderPass const & pass,
			std::vector<std::pair<char const *, char const *>> const & api_special_macros,
			char const * func_name, char const * shader_profile, uint32_t flags);
		static void ReflectDXBC(std::vector<uint8_t> const & code, void** reflector);
		static std::vector<uint8_t> StripDXBC(std::vector<uint8_t> const & code, uint32_t strip_flags);

	protected:
		std::shared_ptr<ShaderObjectTemplate> so_template_;
		
		bool is_validate_;
		bool has_discard_ = false;
	};
}

#endif			// _SHADEROBJECT_HPP
