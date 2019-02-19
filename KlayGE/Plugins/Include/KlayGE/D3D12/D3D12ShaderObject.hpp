/**
 * @file D3D12ShaderObject.hpp
 * @author Minmin Gong
 *
 * @section DESCRIPTION
 *
 * This source file is part of KlayGE
 * For the latest info, see http://www.klayge.org
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * You may alternatively use this source under the terms of
 * the KlayGE Proprietary License (KPL). You can obtained such a license
 * from http://www.klayge.org/licensing/.
 */

#ifndef _D3D12SHADEROBJECT_HPP
#define _D3D12SHADEROBJECT_HPP

#pragma once

#include <KlayGE/PreDeclare.hpp>
#include <KlayGE/ShaderObject.hpp>

#include <KlayGE/D3D12/D3D12Typedefs.hpp>

#if KLAYGE_IS_DEV_PLATFORM
struct ID3D12ShaderReflection;
#endif

namespace KlayGE
{
	struct D3D12ShaderDesc
	{
		struct ConstantBufferDesc
		{
			struct VariableDesc
			{
				std::string name;
				uint32_t start_offset;
				uint8_t type;
				uint8_t rows;
				uint8_t columns;
				uint16_t elements;
			};
			std::vector<VariableDesc> var_desc;

			std::string name;
			size_t name_hash;
			uint32_t size = 0;
		};
		std::vector<ConstantBufferDesc> cb_desc;

		uint16_t num_samplers = 0;
		uint16_t num_srvs = 0;
		uint16_t num_uavs = 0;

		struct BoundResourceDesc
		{
			std::string name;
			uint8_t type;
			uint8_t dimension;
			uint16_t bind_point;
		};
		std::vector<BoundResourceDesc> res_desc;
	};

	class D3D12ShaderStageObject
	{
	public:
		explicit D3D12ShaderStageObject(ShaderObject::ShaderType stage);
		virtual ~D3D12ShaderStageObject();

		void StreamIn(RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids,
			std::vector<uint8_t> const& native_shader_block);
		void StreamOut(std::ostream& os);
		void AttachShader(RenderEffect const& effect, RenderTechnique const& tech, RenderPass const& pass,
			std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids);

		bool Validate() const
		{
			return is_validate_;
		}

		std::vector<uint8_t> const& ShaderCodeBlob() const
		{
			return shader_code_;
		}

		std::string const& ShaderProfile() const
		{
			return shader_profile_;
		}

		D3D12ShaderDesc const& GetD3D12ShaderDesc() const
		{
			return shader_desc_;
		}

		std::vector<uint8_t> const& CBufferIndices() const
		{
			return cbuff_indices_;
		}

		virtual bool HasStreamOutput() const
		{
			return false;
		}

		virtual void UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const;
		virtual void UpdatePsoDesc(D3D12_COMPUTE_PIPELINE_STATE_DESC& pso_desc) const;

	private:
		std::string_view GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const;
		void FillCBufferIndices(RenderEffect const& effect);
		void CreateHwShader(RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids);

		virtual void StageSpecificStreamIn(std::istream& native_shader_stream)
		{
			KFL_UNUSED(native_shader_stream);
		}
		virtual void StageSpecificStreamOut(std::ostream& os)
		{
			KFL_UNUSED(os);
		}
#if KLAYGE_IS_DEV_PLATFORM
		virtual void StageSpecificReflection(ID3D12ShaderReflection* reflection)
		{
			KFL_UNUSED(reflection);
		}
#endif
		virtual void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids)
		{
			KFL_UNUSED(effect);
			KFL_UNUSED(shader_desc_ids);
		}

	protected:
		const ShaderObject::ShaderType stage_;

		bool is_available_;
		bool is_validate_;

		std::vector<uint8_t> shader_code_;
		std::string shader_profile_;
		D3D12ShaderDesc shader_desc_;
		std::vector<uint8_t> cbuff_indices_;
	};

	class D3D12VertexShaderStageObject : public D3D12ShaderStageObject
	{
	public:
		D3D12VertexShaderStageObject();

		bool HasStreamOutput() const override
		{
			return !so_decl_.empty();
		}

		void UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const override;

	private:
		void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids) override;

	private:
		std::vector<D3D12_SO_DECLARATION_ENTRY> so_decl_;
		uint32_t rasterized_stream_ = 0;
	};

	class D3D12PixelShaderStageObject : public D3D12ShaderStageObject
	{
	public:
		D3D12PixelShaderStageObject();

		void UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const override;
	};

	class D3D12GeometryShaderStageObject : public D3D12ShaderStageObject
	{
	public:
		D3D12GeometryShaderStageObject();

		bool HasStreamOutput() const override
		{
			return !so_decl_.empty();
		}

		void UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const override;

	private:
		void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids) override;

	private:
		std::vector<D3D12_SO_DECLARATION_ENTRY> so_decl_;
		uint32_t rasterized_stream_ = 0;
	};

	class D3D12ComputeShaderStageObject : public D3D12ShaderStageObject
	{
	public:
		D3D12ComputeShaderStageObject();

		uint32_t CsBlockSizeX() const
		{
			return cs_block_size_x_;
		}
		uint32_t CsBlockSizeY() const
		{
			return cs_block_size_y_;
		}
		uint32_t CsBlockSizeZ() const
		{
			return cs_block_size_z_;
		}

		void UpdatePsoDesc(D3D12_COMPUTE_PIPELINE_STATE_DESC& pso_desc) const override;

	private:
		void StageSpecificStreamIn(std::istream& native_shader_stream) override;
		void StageSpecificStreamOut(std::ostream& os) override;
#if KLAYGE_IS_DEV_PLATFORM
		void StageSpecificReflection(ID3D12ShaderReflection* reflection) override;
#endif
		void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids) override;

	private:
		uint32_t cs_block_size_x_, cs_block_size_y_, cs_block_size_z_;
	};

	class D3D12HullShaderStageObject : public D3D12ShaderStageObject
	{
	public:
		D3D12HullShaderStageObject();

		void UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const override;

	private:
		void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids) override;
	};

	class D3D12DomainShaderStageObject : public D3D12ShaderStageObject
	{
	public:
		D3D12DomainShaderStageObject();

		bool HasStreamOutput() const override
		{
			return !so_decl_.empty();
		}

		void UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const override;

	private:
		void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids) override;

	private:
		std::vector<D3D12_SO_DECLARATION_ENTRY> so_decl_;
		uint32_t rasterized_stream_ = 0;
	};

	class D3D12ShaderObject : public ShaderObject
	{
	public:
		D3D12ShaderObject();

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

		std::vector<D3D12_SAMPLER_DESC> const & Samplers(ShaderType type) const
		{
			return samplers_[type];
		}

		std::vector<D3D12ShaderResourceViewSimulation*> const & SRVs(ShaderType type) const
		{
			return srvs_[type];
		}

		std::vector<D3D12UnorderedAccessViewSimulation*> const & UAVs(ShaderType type) const
		{
			return uavs_[type];
		}

		std::vector<GraphicsBuffer*> const & CBuffers(ShaderType type) const
		{
			return d3d_cbuffs_[type];
		}

		ID3D12RootSignature* RootSignature() const
		{
			return so_template_->root_signature_.get();
		}
		ID3D12DescriptorHeap* SamplerHeap() const
		{
			return so_template_->sampler_heap_.get();
		}

		void* ShaderObjectTemplate()
		{
			return so_template_.get();
		}
		void const * ShaderObjectTemplate() const
		{
			return so_template_.get();
		}

		void UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc);
		void UpdatePsoDesc(D3D12_COMPUTE_PIPELINE_STATE_DESC& pso_desc);

		uint32_t NumHandles() const
		{
			return num_handles_;
		}

	private:
		struct D3D12ShaderObjectTemplate
		{
			ID3D12RootSignaturePtr root_signature_;
			ID3D12DescriptorHeapPtr sampler_heap_;

			std::array<std::shared_ptr<D3D12ShaderStageObject>, ST_NumShaderTypes> shader_stages_;
		};

		struct ParameterBind
		{
			RenderEffectParameter* param;
			uint32_t offset;
			std::function<void()> func;
		};

	public:
		explicit D3D12ShaderObject(std::shared_ptr<D3D12ShaderObjectTemplate> const & so_template);

	private:
		ParameterBind GetBindFunc(ShaderType type, uint32_t offset, RenderEffectParameter* param);

		void CreateShaderStage(ShaderType stage);
		void CreateHwResources(ShaderType stage, RenderEffect const & effect);
		void CreateRootSignature();

	private:
		std::shared_ptr<D3D12ShaderObjectTemplate> so_template_;

		std::array<std::vector<ParameterBind>, ST_NumShaderTypes> param_binds_;

		std::array<std::vector<D3D12_SAMPLER_DESC>, ST_NumShaderTypes> samplers_;
		std::array<std::vector<std::tuple<D3D12Resource*, uint32_t, uint32_t>>, ST_NumShaderTypes> srvsrcs_;
		std::array<std::vector<D3D12ShaderResourceViewSimulation*>, ST_NumShaderTypes> srvs_;
		std::array<std::vector<D3D12Resource*>, ST_NumShaderTypes> uavsrcs_;
		std::array<std::vector<D3D12UnorderedAccessViewSimulation*>, ST_NumShaderTypes> uavs_;
		std::array<std::vector<GraphicsBuffer*>, ST_NumShaderTypes> d3d_cbuffs_;

		std::vector<RenderEffectConstantBuffer*> all_cbuffs_;

		uint32_t num_handles_;
	};

	typedef std::shared_ptr<D3D12ShaderObject> D3D12ShaderObjectPtr;
}

#endif			// _D3D12SHADEROBJECT_HPP
