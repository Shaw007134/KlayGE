// D3D11ShaderObject.hpp
// KlayGE D3D11 shader对象类 头文件
// Ver 3.8.0
// 版权所有(C) 龚敏敏, 2009
// Homepage: http://www.klayge.org
//
// 3.8.0
// 初次建立 (2009.1.30)
//
// 修改记录
/////////////////////////////////////////////////////////////////////////////////

#ifndef _D3D11SHADEROBJECT_HPP
#define _D3D11SHADEROBJECT_HPP

#pragma once

#include <KlayGE/PreDeclare.hpp>
#include <KlayGE/ShaderObject.hpp>
#include <KFL/ArrayRef.hpp>

#include <KlayGE/D3D11/D3D11Typedefs.hpp>

struct ID3D11ShaderReflection;

namespace KlayGE
{
	struct D3D11ShaderDesc
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

	class D3D11ShaderStageObject
	{
	public:
		explicit D3D11ShaderStageObject(ShaderObject::ShaderType stage);
		virtual ~D3D11ShaderStageObject();

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

		D3D11ShaderDesc const& GetD3D11ShaderDesc() const
		{
			return shader_desc_;
		}

		std::vector<uint8_t> const& CBufferIndices() const
		{
			return cbuff_indices_;
		}

		virtual ID3D11VertexShader* HwVertexShader() const
		{
			return nullptr;
		}
		virtual ID3D11PixelShader* HwPixelShader() const
		{
			return nullptr;
		}
		virtual ID3D11GeometryShader* HwGeometryShader() const
		{
			return nullptr;
		}
		virtual ID3D11ComputeShader* HwComputeShader() const
		{
			return nullptr;
		}
		virtual ID3D11HullShader* HwHullShader() const
		{
			return nullptr;
		}
		virtual ID3D11DomainShader* HwDomainShader() const
		{
			return nullptr;
		}

	protected:
		ID3D11GeometryShaderPtr CreateGeometryShaderWithStreamOutput(RenderEffect const& effect,
			std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids, ArrayRef<uint8_t> code_blob,
			std::vector<ShaderDesc::StreamOutputDecl> const& so_decl);

	private:
		std::string_view GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const;
		void FillCBufferIndices(RenderEffect const& effect);
		void CreateHwShader(RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids);
		virtual void ClearHwShader() = 0;

		virtual void StageSpecificStreamIn(std::istream& native_shader_stream)
		{
			KFL_UNUSED(native_shader_stream);
		}
		virtual void StageSpecificStreamOut(std::ostream& os)
		{
			KFL_UNUSED(os);
		}
		virtual void StageSpecificReflection(ID3D11ShaderReflection* reflection)
		{
			KFL_UNUSED(reflection);
		}
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
		D3D11ShaderDesc shader_desc_;
		std::vector<uint8_t> cbuff_indices_;
	};

	class D3D11VertexShaderStageObject : public D3D11ShaderStageObject
	{
	public:
		D3D11VertexShaderStageObject();

		ID3D11VertexShader* HwVertexShader() const override
		{
			return vertex_shader_.get();
		}
		ID3D11GeometryShader* HwGeometryShader() const override
		{
			return geometry_shader_.get();
		}

		uint32_t VsSignature() const
		{
			return vs_signature_;
		}

	private:
		void ClearHwShader() override;

		void StageSpecificStreamIn(std::istream& native_shader_stream) override;
		void StageSpecificStreamOut(std::ostream& os) override;
		void StageSpecificReflection(ID3D11ShaderReflection* reflection) override;
		void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids) override;

	private:
		ID3D11VertexShaderPtr vertex_shader_;
		ID3D11GeometryShaderPtr geometry_shader_;

		uint32_t vs_signature_;
	};

	class D3D11PixelShaderStageObject : public D3D11ShaderStageObject
	{
	public:
		D3D11PixelShaderStageObject();

		ID3D11PixelShader* HwPixelShader() const override
		{
			return pixel_shader_.get();
		}

	private:
		void ClearHwShader() override;
		void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids) override;

	private:
		ID3D11PixelShaderPtr pixel_shader_;
	};

	class D3D11GeometryShaderStageObject : public D3D11ShaderStageObject
	{
	public:
		D3D11GeometryShaderStageObject();

		ID3D11GeometryShader* HwGeometryShader() const override
		{
			return geometry_shader_.get();
		}

	private:
		void ClearHwShader() override;
		void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids) override;

	private:
		ID3D11GeometryShaderPtr geometry_shader_;
	};

	class D3D11ComputeShaderStageObject : public D3D11ShaderStageObject
	{
	public:
		D3D11ComputeShaderStageObject();

		ID3D11ComputeShader* HwComputeShader() const override
		{
			return compute_shader_.get();
		}

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

	private:
		void ClearHwShader() override;
		void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids) override;

		void StageSpecificStreamIn(std::istream& native_shader_stream) override;
		void StageSpecificStreamOut(std::ostream& os) override;
		void StageSpecificReflection(ID3D11ShaderReflection* reflection) override;

	private:
		ID3D11ComputeShaderPtr compute_shader_;

		uint32_t cs_block_size_x_, cs_block_size_y_, cs_block_size_z_;
	};

	class D3D11HullShaderStageObject : public D3D11ShaderStageObject
	{
	public:
		D3D11HullShaderStageObject();

		ID3D11HullShader* HwHullShader() const override
		{
			return hull_shader_.get();
		}

	private:
		void ClearHwShader() override;
		void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids) override;

	private:
		ID3D11HullShaderPtr hull_shader_;
	};

	class D3D11DomainShaderStageObject : public D3D11ShaderStageObject
	{
	public:
		D3D11DomainShaderStageObject();

		ID3D11DomainShader* HwDomainShader() const override
		{
			return domain_shader_.get();
		}
		ID3D11GeometryShader* HwGeometryShader() const override
		{
			return geometry_shader_.get();
		}

	private:
		void ClearHwShader() override;
		void StageSpecificCreateHwShader(
			RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids) override;

	private:
		ID3D11DomainShaderPtr domain_shader_;
		ID3D11GeometryShaderPtr geometry_shader_;
	};

	class D3D11ShaderObject : public ShaderObject
	{
	public:
		D3D11ShaderObject();

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

		void Bind() override;
		void Unbind() override;

		ArrayRef<uint8_t> VsCode() const;
		uint32_t VsSignature() const;

	private:
		struct D3D11ShaderObjectTemplate
		{
			std::array<std::shared_ptr<D3D11ShaderStageObject>, ST_NumShaderTypes> shader_stages_;
		};

		struct ParameterBind
		{
			RenderEffectParameter* param;
			uint32_t offset;
			std::function<void()> func;
		};

	public:
		explicit D3D11ShaderObject(std::shared_ptr<D3D11ShaderObjectTemplate> const & so_template);

	private:
		ParameterBind GetBindFunc(ShaderType type, uint32_t offset, RenderEffectParameter* param);

		void CreateShaderStage(ShaderType stage);
		void CreateHwResources(ShaderType stage, RenderEffect const & effect);

	private:
		std::shared_ptr<D3D11ShaderObjectTemplate> so_template_;

		std::array<std::vector<ParameterBind>, ST_NumShaderTypes> param_binds_;

		std::array<std::vector<ID3D11SamplerState*>, ST_NumShaderTypes> samplers_;
		std::array<std::vector<std::tuple<void*, uint32_t, uint32_t>>, ST_NumShaderTypes> srvsrcs_;
		std::array<std::vector<ID3D11ShaderResourceView*>, ST_NumShaderTypes> srvs_;
		std::array<std::vector<ID3D11Buffer*>, ST_NumShaderTypes> d3d11_cbuffs_;
		std::vector<void*> uavsrcs_;
		std::vector<ID3D11UnorderedAccessView*> uavs_;

		std::vector<RenderEffectConstantBuffer*> all_cbuffs_;
	};

	typedef std::shared_ptr<D3D11ShaderObject> D3D11ShaderObjectPtr;
}

#endif			// _D3D11SHADEROBJECT_HPP
