/**
 * @file D3D12ShaderObject.cpp
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

#include <KlayGE/KlayGE.hpp>
#define INITGUID
#include <KFL/ErrorHandling.hpp>
#include <KFL/Util.hpp>
#include <KFL/Math.hpp>
#include <KFL/COMPtr.hpp>
#include <KFL/ResIdentifier.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderFactory.hpp>
#include <KlayGE/RenderEffect.hpp>
#include <KFL/CustomizedStreamBuf.hpp>
#include <KFL/Hash.hpp>

#include <string>
#include <algorithm>
#include <cstring>
#include <boost/assert.hpp>

#ifdef KLAYGE_PLATFORM_WINDOWS_DESKTOP
#include <KlayGE/SALWrapper.hpp>
#include <d3dcompiler.h>
#endif

#include <KlayGE/D3D12/D3D12RenderEngine.hpp>
#include <KlayGE/D3D12/D3D12RenderStateObject.hpp>
#include <KlayGE/D3D12/D3D12Mapping.hpp>
#include <KlayGE/D3D12/D3D12Texture.hpp>
#include <KlayGE/D3D12/D3D12InterfaceLoader.hpp>
#include <KlayGE/D3D12/D3D12ShaderObject.hpp>

namespace
{
	using namespace KlayGE;

	class SetD3D12ShaderParameterTextureSRV
	{
	public:
		SetD3D12ShaderParameterTextureSRV(std::tuple<D3D12Resource*, uint32_t, uint32_t>& srvsrc,
				D3D12ShaderResourceViewSimulation*& srv, RenderEffectParameter* param)
			: srvsrc_(&srvsrc), srv_(&srv), param_(param)
		{
		}

		void operator()()
		{
			ShaderResourceViewPtr srv;
			param_->Value(srv);
			if (srv)
			{
				if (srv->TextureResource())
				{
					*srvsrc_ = std::make_tuple(checked_cast<D3D12Texture*>(srv->TextureResource().get()),
						srv->FirstArrayIndex() * srv->TextureResource()->NumMipMaps() + srv->FirstLevel(),
						srv->ArraySize() * srv->NumLevels());
				}
				else
				{
					std::get<0>(*srvsrc_) = nullptr;
				}
				*srv_ = checked_cast<D3D12ShaderResourceView*>(srv.get())->RetrieveD3DShaderResourceView().get();
			}
			else
			{
				std::get<0>(*srvsrc_) = nullptr;
				*srv_ = nullptr;
			}
		}

	private:
		std::tuple<D3D12Resource*, uint32_t, uint32_t>* srvsrc_;
		D3D12ShaderResourceViewSimulation** srv_;
		RenderEffectParameter* param_;
	};

	class SetD3D12ShaderParameterGraphicsBufferSRV
	{
	public:
		SetD3D12ShaderParameterGraphicsBufferSRV(std::tuple<D3D12Resource*, uint32_t, uint32_t>& srvsrc,
				D3D12ShaderResourceViewSimulation*& srv, RenderEffectParameter* param)
			: srvsrc_(&srvsrc), srv_(&srv), param_(param)
		{
		}

		void operator()()
		{
			ShaderResourceViewPtr srv;
			param_->Value(srv);
			if (srv)
			{
				if (srv->BufferResource())
				{
					*srvsrc_ = std::make_tuple(checked_cast<D3D12GraphicsBuffer*>(srv->BufferResource().get()), 0, 1);
				}
				else
				{
					std::get<0>(*srvsrc_) = nullptr;
				}
				*srv_ = checked_cast<D3D12ShaderResourceView*>(srv.get())->RetrieveD3DShaderResourceView().get();
			}
			else
			{
				std::get<0>(*srvsrc_) = nullptr;
				*srv_ = nullptr;
			}
		}

	private:
		std::tuple<D3D12Resource*, uint32_t, uint32_t>* srvsrc_;
		D3D12ShaderResourceViewSimulation** srv_;
		RenderEffectParameter* param_;
	};

	class SetD3D12ShaderParameterTextureUAV
	{
	public:
		SetD3D12ShaderParameterTextureUAV(D3D12Resource*& uavsrc,
				D3D12UnorderedAccessViewSimulation*& uav, RenderEffectParameter* param)
			: uavsrc_(&uavsrc), uav_(&uav), param_(param)
		{
		}

		void operator()()
		{
			UnorderedAccessViewPtr uav;
			param_->Value(uav);
			if (uav)
			{
				*uavsrc_ = checked_cast<D3D12Texture*>(uav->TextureResource().get());
				*uav_ = checked_cast<D3D12UnorderedAccessView*>(uav.get())->RetrieveD3DUnorderedAccessView();
			}
			else
			{
				*uavsrc_ = nullptr;
				*uav_ = nullptr;
			}
		}

	private:
		D3D12Resource** uavsrc_;
		D3D12UnorderedAccessViewSimulation** uav_;
		RenderEffectParameter* param_;
	};

	class SetD3D12ShaderParameterGraphicsBufferUAV
	{
	public:
		SetD3D12ShaderParameterGraphicsBufferUAV(D3D12Resource*& uavsrc,
				D3D12UnorderedAccessViewSimulation*& uav, RenderEffectParameter* const & param)
			: uavsrc_(&uavsrc), uav_(&uav), param_(param)
		{
		}

		void operator()()
		{
			UnorderedAccessViewPtr uav;
			param_->Value(uav);
			if (uav)
			{
				*uavsrc_ = checked_cast<D3D12GraphicsBuffer*>(uav->BufferResource().get());
				*uav_ = checked_cast<D3D12UnorderedAccessView*>(uav.get())->RetrieveD3DUnorderedAccessView();
			}
			else
			{
				*uavsrc_ = nullptr;
				*uav_ = nullptr;
			}
		}

	private:
		D3D12Resource** uavsrc_;
		D3D12UnorderedAccessViewSimulation** uav_;
		RenderEffectParameter* param_;
	};
}

namespace KlayGE
{
	D3D12ShaderStageObject::D3D12ShaderStageObject(ShaderObject::ShaderType stage) : stage_(stage)
	{
	}

	D3D12ShaderStageObject::~D3D12ShaderStageObject() = default;

	void D3D12ShaderStageObject::UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const
	{
		KFL_UNUSED(pso_desc);
		KFL_UNREACHABLE("Couldn't update graphics pipeline state for this shader stage.");
	}

	void D3D12ShaderStageObject::UpdatePsoDesc(D3D12_COMPUTE_PIPELINE_STATE_DESC& pso_desc) const
	{
		KFL_UNUSED(pso_desc);
		KFL_UNREACHABLE("Couldn't update compute pipeline state for this shader stage.");
	}

	void D3D12ShaderStageObject::StreamIn(RenderEffect const& effect,
		std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids, std::vector<uint8_t> const& native_shader_block)
	{
		is_validate_ = false;
		std::string_view shader_profile = this->GetShaderProfile(effect, shader_desc_ids[stage_]);
		if (native_shader_block.size() >= 25 + shader_profile.size())
		{
			MemInputStreamBuf native_shader_buff(native_shader_block.data(), native_shader_block.size());
			std::istream native_shader_stream(&native_shader_buff);

			uint8_t len;
			native_shader_stream.read(reinterpret_cast<char*>(&len), sizeof(len));
			std::string& profile = shader_profile_;
			profile.resize(len);
			native_shader_stream.read(&profile[0], len);
			if (profile == shader_profile)
			{
				is_validate_ = true;

				uint32_t blob_size;
				native_shader_stream.read(reinterpret_cast<char*>(&blob_size), sizeof(blob_size));
				shader_code_.resize(blob_size);

				native_shader_stream.read(reinterpret_cast<char*>(shader_code_.data()), blob_size);

				uint16_t cb_desc_size;
				native_shader_stream.read(reinterpret_cast<char*>(&cb_desc_size), sizeof(cb_desc_size));
				cb_desc_size = LE2Native(cb_desc_size);
				shader_desc_.cb_desc.resize(cb_desc_size);
				for (size_t i = 0; i < shader_desc_.cb_desc.size(); ++i)
				{
					native_shader_stream.read(reinterpret_cast<char*>(&len), sizeof(len));
					shader_desc_.cb_desc[i].name.resize(len);
					native_shader_stream.read(&shader_desc_.cb_desc[i].name[0], len);

					shader_desc_.cb_desc[i].name_hash = RT_HASH(shader_desc_.cb_desc[i].name.c_str());

					native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.cb_desc[i].size), sizeof(shader_desc_.cb_desc[i].size));
					shader_desc_.cb_desc[i].size = LE2Native(shader_desc_.cb_desc[i].size);

					uint16_t var_desc_size;
					native_shader_stream.read(reinterpret_cast<char*>(&var_desc_size), sizeof(var_desc_size));
					var_desc_size = LE2Native(var_desc_size);
					shader_desc_.cb_desc[i].var_desc.resize(var_desc_size);
					for (size_t j = 0; j < shader_desc_.cb_desc[i].var_desc.size(); ++j)
					{
						native_shader_stream.read(reinterpret_cast<char*>(&len), sizeof(len));
						shader_desc_.cb_desc[i].var_desc[j].name.resize(len);
						native_shader_stream.read(&shader_desc_.cb_desc[i].var_desc[j].name[0], len);

						native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.cb_desc[i].var_desc[j].start_offset),
							sizeof(shader_desc_.cb_desc[i].var_desc[j].start_offset));
						shader_desc_.cb_desc[i].var_desc[j].start_offset = LE2Native(shader_desc_.cb_desc[i].var_desc[j].start_offset);
						native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.cb_desc[i].var_desc[j].type),
							sizeof(shader_desc_.cb_desc[i].var_desc[j].type));
						native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.cb_desc[i].var_desc[j].rows),
							sizeof(shader_desc_.cb_desc[i].var_desc[j].rows));
						native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.cb_desc[i].var_desc[j].columns),
							sizeof(shader_desc_.cb_desc[i].var_desc[j].columns));
						native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.cb_desc[i].var_desc[j].elements),
							sizeof(shader_desc_.cb_desc[i].var_desc[j].elements));
						shader_desc_.cb_desc[i].var_desc[j].elements = LE2Native(shader_desc_.cb_desc[i].var_desc[j].elements);
					}
				}

				native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.num_samplers), sizeof(shader_desc_.num_samplers));
				shader_desc_.num_samplers = LE2Native(shader_desc_.num_samplers);
				native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.num_srvs), sizeof(shader_desc_.num_srvs));
				shader_desc_.num_srvs = LE2Native(shader_desc_.num_srvs);
				native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.num_uavs), sizeof(shader_desc_.num_uavs));
				shader_desc_.num_uavs = LE2Native(shader_desc_.num_uavs);

				uint16_t res_desc_size;
				native_shader_stream.read(reinterpret_cast<char*>(&res_desc_size), sizeof(res_desc_size));
				res_desc_size = LE2Native(res_desc_size);
				shader_desc_.res_desc.resize(res_desc_size);
				for (size_t i = 0; i < shader_desc_.res_desc.size(); ++i)
				{
					native_shader_stream.read(reinterpret_cast<char*>(&len), sizeof(len));
					shader_desc_.res_desc[i].name.resize(len);
					native_shader_stream.read(&shader_desc_.res_desc[i].name[0], len);

					native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.res_desc[i].type),
						sizeof(shader_desc_.res_desc[i].type));

					native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.res_desc[i].dimension),
						sizeof(shader_desc_.res_desc[i].dimension));

					native_shader_stream.read(reinterpret_cast<char*>(&shader_desc_.res_desc[i].bind_point),
						sizeof(shader_desc_.res_desc[i].bind_point));
					shader_desc_.res_desc[i].bind_point = LE2Native(shader_desc_.res_desc[i].bind_point);
				}

				this->FillCBufferIndices(effect);

				this->StageSpecificStreamIn(native_shader_stream);

				this->CreateHwShader(effect, shader_desc_ids);
			}
		}
	}

	void D3D12ShaderStageObject::StreamOut(std::ostream& os)
	{
		std::vector<char> native_shader_block;
		VectorOutputStreamBuf native_shader_buff(native_shader_block);
		std::ostream oss(&native_shader_buff);

		{
			uint8_t len = static_cast<uint8_t>(shader_profile_.size());
			oss.write(reinterpret_cast<char const *>(&len), sizeof(len));
			oss.write(reinterpret_cast<char const *>(shader_profile_.data()), len);
		}

		if (!shader_code_.empty())
		{
			uint8_t len;

			uint32_t blob_size = Native2LE(static_cast<uint32_t>(shader_code_.size()));
			oss.write(reinterpret_cast<char const*>(&blob_size), sizeof(blob_size));
			oss.write(reinterpret_cast<char const*>(shader_code_.data()), shader_code_.size());

			uint16_t cb_desc_size = Native2LE(static_cast<uint16_t>(shader_desc_.cb_desc.size()));
			oss.write(reinterpret_cast<char const*>(&cb_desc_size), sizeof(cb_desc_size));
			for (size_t i = 0; i < shader_desc_.cb_desc.size(); ++i)
			{
				len = static_cast<uint8_t>(shader_desc_.cb_desc[i].name.size());
				oss.write(reinterpret_cast<char const*>(&len), sizeof(len));
				oss.write(reinterpret_cast<char const*>(&shader_desc_.cb_desc[i].name[0]), len);

				uint32_t size = Native2LE(shader_desc_.cb_desc[i].size);
				oss.write(reinterpret_cast<char const*>(&size), sizeof(size));

				uint16_t var_desc_size = Native2LE(static_cast<uint16_t>(shader_desc_.cb_desc[i].var_desc.size()));
				oss.write(reinterpret_cast<char const*>(&var_desc_size), sizeof(var_desc_size));
				for (size_t j = 0; j < shader_desc_.cb_desc[i].var_desc.size(); ++j)
				{
					len = static_cast<uint8_t>(shader_desc_.cb_desc[i].var_desc[j].name.size());
					oss.write(reinterpret_cast<char const*>(&len), sizeof(len));
					oss.write(reinterpret_cast<char const*>(&shader_desc_.cb_desc[i].var_desc[j].name[0]), len);

					uint32_t start_offset = Native2LE(shader_desc_.cb_desc[i].var_desc[j].start_offset);
					oss.write(reinterpret_cast<char const*>(&start_offset), sizeof(start_offset));
					oss.write(reinterpret_cast<char const*>(&shader_desc_.cb_desc[i].var_desc[j].type),
						sizeof(shader_desc_.cb_desc[i].var_desc[j].type));
					oss.write(reinterpret_cast<char const*>(&shader_desc_.cb_desc[i].var_desc[j].rows),
						sizeof(shader_desc_.cb_desc[i].var_desc[j].rows));
					oss.write(reinterpret_cast<char const*>(&shader_desc_.cb_desc[i].var_desc[j].columns),
						sizeof(shader_desc_.cb_desc[i].var_desc[j].columns));
					uint16_t elements = Native2LE(shader_desc_.cb_desc[i].var_desc[j].elements);
					oss.write(reinterpret_cast<char const*>(&elements), sizeof(elements));
				}
			}

			uint16_t num_samplers = Native2LE(shader_desc_.num_samplers);
			oss.write(reinterpret_cast<char const*>(&num_samplers), sizeof(num_samplers));
			uint16_t num_srvs = Native2LE(shader_desc_.num_srvs);
			oss.write(reinterpret_cast<char const*>(&num_srvs), sizeof(num_srvs));
			uint16_t num_uavs = Native2LE(shader_desc_.num_uavs);
			oss.write(reinterpret_cast<char const*>(&num_uavs), sizeof(num_uavs));

			uint16_t res_desc_size = Native2LE(static_cast<uint16_t>(shader_desc_.res_desc.size()));
			oss.write(reinterpret_cast<char const*>(&res_desc_size), sizeof(res_desc_size));
			for (size_t i = 0; i < shader_desc_.res_desc.size(); ++i)
			{
				len = static_cast<uint8_t>(shader_desc_.res_desc[i].name.size());
				oss.write(reinterpret_cast<char const*>(&len), sizeof(len));
				oss.write(reinterpret_cast<char const*>(&shader_desc_.res_desc[i].name[0]), len);

				oss.write(reinterpret_cast<char const*>(&shader_desc_.res_desc[i].type), sizeof(shader_desc_.res_desc[i].type));
				oss.write(reinterpret_cast<char const*>(&shader_desc_.res_desc[i].dimension), sizeof(shader_desc_.res_desc[i].dimension));
				uint16_t bind_point = Native2LE(shader_desc_.res_desc[i].bind_point);
				oss.write(reinterpret_cast<char const*>(&bind_point), sizeof(bind_point));
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

	void D3D12ShaderStageObject::AttachShader(RenderEffect const& effect, RenderTechnique const& tech, RenderPass const& pass,
		std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids)
	{
		shader_code_.clear();

#ifdef KLAYGE_PLATFORM_WINDOWS_DESKTOP
		auto const& sd = effect.GetShaderDesc(shader_desc_ids[stage_]);

		shader_profile_ = std::string(this->GetShaderProfile(effect, shader_desc_ids[stage_]));
		is_validate_ = !shader_profile_.empty();

		if (is_validate_)
		{
			std::vector<std::pair<char const *, char const *>> macros;
			macros.emplace_back("KLAYGE_D3D12", "1");
			macros.emplace_back("KLAYGE_FRAG_DEPTH", "1");

			uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if !defined(KLAYGE_DEBUG)
			flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
			shader_code_ =
				ShaderObject::CompileToDXBC(stage_, effect, tech, pass, macros, sd.func_name.c_str(), shader_profile_.c_str(), flags);

			if (!shader_code_.empty())
			{
				ID3D12ShaderReflection* reflection;
				ShaderObject::ReflectDXBC(shader_code_, reinterpret_cast<void**>(&reflection));
				if (reflection != nullptr)
				{
					D3D12_SHADER_DESC desc;
					reflection->GetDesc(&desc);

					for (UINT c = 0; c < desc.ConstantBuffers; ++c)
					{
						ID3D12ShaderReflectionConstantBuffer* reflection_cb = reflection->GetConstantBufferByIndex(c);

						D3D12_SHADER_BUFFER_DESC d3d_cb_desc;
						reflection_cb->GetDesc(&d3d_cb_desc);
						if ((D3D_CT_CBUFFER == d3d_cb_desc.Type) || (D3D_CT_TBUFFER == d3d_cb_desc.Type))
						{
							D3D12ShaderDesc::ConstantBufferDesc cb_desc;
							cb_desc.name = d3d_cb_desc.Name;
							cb_desc.name_hash = RT_HASH(d3d_cb_desc.Name);
							cb_desc.size = d3d_cb_desc.Size;

							for (UINT v = 0; v < d3d_cb_desc.Variables; ++v)
							{
								ID3D12ShaderReflectionVariable* reflection_var = reflection_cb->GetVariableByIndex(v);

								D3D12_SHADER_VARIABLE_DESC var_desc;
								reflection_var->GetDesc(&var_desc);

								D3D12_SHADER_TYPE_DESC type_desc;
								reflection_var->GetType()->GetDesc(&type_desc);

								D3D12ShaderDesc::ConstantBufferDesc::VariableDesc vd;
								vd.name = var_desc.Name;
								vd.start_offset = var_desc.StartOffset;
								vd.type = static_cast<uint8_t>(type_desc.Type);
								vd.rows = static_cast<uint8_t>(type_desc.Rows);
								vd.columns = static_cast<uint8_t>(type_desc.Columns);
								vd.elements = static_cast<uint16_t>(type_desc.Elements);
								cb_desc.var_desc.push_back(vd);
							}

							shader_desc_.cb_desc.push_back(cb_desc);
						}
					}

					this->FillCBufferIndices(effect);

					int max_sampler_bind_pt = -1;
					int max_srv_bind_pt = -1;
					int max_uav_bind_pt = -1;
					for (uint32_t i = 0; i < desc.BoundResources; ++i)
					{
						D3D12_SHADER_INPUT_BIND_DESC si_desc;
						reflection->GetResourceBindingDesc(i, &si_desc);

						switch (si_desc.Type)
						{
						case D3D_SIT_SAMPLER:
							max_sampler_bind_pt = std::max(max_sampler_bind_pt, static_cast<int>(si_desc.BindPoint));
							break;

						case D3D_SIT_TEXTURE:
						case D3D_SIT_STRUCTURED:
						case D3D_SIT_BYTEADDRESS:
							max_srv_bind_pt = std::max(max_srv_bind_pt, static_cast<int>(si_desc.BindPoint));
							break;

						case D3D_SIT_UAV_RWTYPED:
						case D3D_SIT_UAV_RWSTRUCTURED:
						case D3D_SIT_UAV_RWBYTEADDRESS:
						case D3D_SIT_UAV_APPEND_STRUCTURED:
						case D3D_SIT_UAV_CONSUME_STRUCTURED:
						case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
							max_uav_bind_pt = std::max(max_uav_bind_pt, static_cast<int>(si_desc.BindPoint));
							break;

						default:
							break;
						}
					}

					shader_desc_.num_samplers = static_cast<uint16_t>(max_sampler_bind_pt + 1);
					shader_desc_.num_srvs = static_cast<uint16_t>(max_srv_bind_pt + 1);
					shader_desc_.num_uavs = static_cast<uint16_t>(max_uav_bind_pt + 1);

					for (uint32_t i = 0; i < desc.BoundResources; ++i)
					{
						D3D12_SHADER_INPUT_BIND_DESC si_desc;
						reflection->GetResourceBindingDesc(i, &si_desc);

						switch (si_desc.Type)
						{
						case D3D_SIT_TEXTURE:
						case D3D_SIT_SAMPLER:
						case D3D_SIT_STRUCTURED:
						case D3D_SIT_BYTEADDRESS:
						case D3D_SIT_UAV_RWTYPED:
						case D3D_SIT_UAV_RWSTRUCTURED:
						case D3D_SIT_UAV_RWBYTEADDRESS:
						case D3D_SIT_UAV_APPEND_STRUCTURED:
						case D3D_SIT_UAV_CONSUME_STRUCTURED:
						case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
							if (effect.ParameterByName(si_desc.Name))
							{
								D3D12ShaderDesc::BoundResourceDesc brd;
								brd.name = si_desc.Name;
								brd.type = static_cast<uint8_t>(si_desc.Type);
								brd.bind_point = static_cast<uint16_t>(si_desc.BindPoint);
								shader_desc_.res_desc.push_back(brd);
							}
							break;

						default:
							break;
						}
					}

					this->StageSpecificReflection(reflection);

					reflection->Release();
				}

				shader_code_ = ShaderObject::StripDXBC(shader_code_, D3DCOMPILER_STRIP_REFLECTION_DATA | D3DCOMPILER_STRIP_DEBUG_INFO |
																		 D3DCOMPILER_STRIP_TEST_BLOBS | D3DCOMPILER_STRIP_PRIVATE_DATA);
			}
		}

		if (shader_code_.empty())
		{
			shader_profile_.clear();
		}
#else
		KFL_UNUSED(type);
		KFL_UNUSED(effect);
		KFL_UNUSED(tech);
		KFL_UNUSED(pass);
		KFL_UNUSED(shader_desc_ids);
#endif

		this->CreateHwShader(effect, shader_desc_ids);
	}
	
	void D3D12ShaderStageObject::CreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids)
	{
		if (!shader_code_.empty())
		{
			auto& rf = Context::Instance().RenderFactoryInstance();
			auto const& re = *checked_cast<D3D12RenderEngine const*>(&rf.RenderEngineInstance());
			auto const& caps = re.DeviceCaps();
			auto const & sd = effect.GetShaderDesc(shader_desc_ids[stage_]);

			uint8_t shader_major_ver = ("auto" == sd.profile) ? 0 : static_cast<uint8_t>(sd.profile[3] - '0');
			uint8_t shader_minor_ver = ("auto" == sd.profile) ? 0 : static_cast<uint8_t>(sd.profile[5] - '0');
			if (ShaderModel(shader_major_ver, shader_minor_ver) > caps.max_shader_model)
			{
				is_validate_ = false;
			}
			else
			{
				is_validate_ = true;
				this->StageSpecificCreateHwShader(effect, shader_desc_ids);
			}
		}
		else
		{
			is_validate_ = false;
		}
	}

	void D3D12ShaderStageObject::FillCBufferIndices(RenderEffect const& effect)
	{
		if (!shader_desc_.cb_desc.empty())
		{
			cbuff_indices_.resize(shader_desc_.cb_desc.size());
		}
		for (size_t c = 0; c < shader_desc_.cb_desc.size(); ++c)
		{
			uint32_t i = 0;
			for (; i < effect.NumCBuffers(); ++i)
			{
				if (effect.CBufferByIndex(i)->NameHash() == shader_desc_.cb_desc[c].name_hash)
				{
					cbuff_indices_[c] = static_cast<uint8_t>(i);
					break;
				}
			}
			BOOST_ASSERT(i < effect.NumCBuffers());
		}
	}

	std::string_view D3D12ShaderStageObject::GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const
	{
		std::string_view shader_profile = effect.GetShaderDesc(shader_desc_id).profile;
		if (is_available_)
		{
			if (shader_profile == "auto")
			{
				auto const& re =
					*checked_cast<D3D12RenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
				shader_profile = re.DefaultShaderProfile(stage_);
			}
		}
		else
		{
			shader_profile = std::string_view();
		}
		return shader_profile;
	}


	D3D12VertexShaderStageObject::D3D12VertexShaderStageObject() : D3D12ShaderStageObject(ShaderObject::ST_VertexShader)
	{
		is_available_ = true;
	}

	void D3D12VertexShaderStageObject::UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const
	{
		pso_desc.VS.pShaderBytecode = shader_code_.data();
		pso_desc.VS.BytecodeLength = static_cast<UINT>(shader_code_.size());

		pso_desc.StreamOutput.pSODeclaration = so_decl_.data();
		pso_desc.StreamOutput.NumEntries = static_cast<UINT>(so_decl_.size());
		pso_desc.StreamOutput.pBufferStrides = nullptr;
		pso_desc.StreamOutput.NumStrides = 0;
		pso_desc.StreamOutput.RasterizedStream = rasterized_stream_;
	}

	void D3D12VertexShaderStageObject::StageSpecificCreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids)
	{
		auto const& re = *checked_cast<D3D12RenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		auto const& caps = re.DeviceCaps();
		auto const& sd = effect.GetShaderDesc(shader_desc_ids[stage_]);
		if (caps.gs_support && !sd.so_decl.empty())
		{
			so_decl_.resize(sd.so_decl.size());
			for (size_t i = 0; i < sd.so_decl.size(); ++i)
			{
				so_decl_[i] = D3D12Mapping::Mapping(sd.so_decl[i]);
			}

			rasterized_stream_ = 0;
			if (effect.GetShaderDesc(shader_desc_ids[ShaderObject::ST_PixelShader]).func_name.empty())
			{
				rasterized_stream_ = D3D12_SO_NO_RASTERIZED_STREAM;
			}
		}
	}


	D3D12PixelShaderStageObject::D3D12PixelShaderStageObject() : D3D12ShaderStageObject(ShaderObject::ST_PixelShader)
	{
		is_available_ = true;
	}

	void D3D12PixelShaderStageObject::UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const
	{
		pso_desc.PS.pShaderBytecode = shader_code_.data();
		pso_desc.PS.BytecodeLength = static_cast<UINT>(shader_code_.size());
	}


	D3D12GeometryShaderStageObject::D3D12GeometryShaderStageObject() : D3D12ShaderStageObject(ShaderObject::ST_GeometryShader)
	{
		auto const& re = *checked_cast<D3D12RenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		auto const& caps = re.DeviceCaps();
		is_available_ = caps.gs_support;
	}

	void D3D12GeometryShaderStageObject::UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const
	{
		pso_desc.GS.pShaderBytecode = shader_code_.data();
		pso_desc.GS.BytecodeLength = static_cast<UINT>(shader_code_.size());

		pso_desc.StreamOutput.pSODeclaration = so_decl_.data();
		pso_desc.StreamOutput.NumEntries = static_cast<UINT>(so_decl_.size());
		pso_desc.StreamOutput.pBufferStrides = nullptr;
		pso_desc.StreamOutput.NumStrides = 0;
		pso_desc.StreamOutput.RasterizedStream = rasterized_stream_;
	}

	void D3D12GeometryShaderStageObject::StageSpecificCreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids)
	{
		if (is_available_)
		{
			ShaderDesc const& sd = effect.GetShaderDesc(shader_desc_ids[stage_]);
			so_decl_.resize(sd.so_decl.size());
			for (size_t i = 0; i < sd.so_decl.size(); ++i)
			{
				so_decl_[i] = D3D12Mapping::Mapping(sd.so_decl[i]);
			}

			rasterized_stream_ = 0;
			if (effect.GetShaderDesc(shader_desc_ids[ShaderObject::ST_PixelShader]).func_name.empty())
			{
				rasterized_stream_ = D3D12_SO_NO_RASTERIZED_STREAM;
			}
		}
		else
		{
			is_validate_ = false;
		}
	}


	D3D12ComputeShaderStageObject::D3D12ComputeShaderStageObject() : D3D12ShaderStageObject(ShaderObject::ST_ComputeShader)
	{
		auto const& re = *checked_cast<D3D12RenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		auto const& caps = re.DeviceCaps();
		is_available_ = caps.cs_support;
	}

	void D3D12ComputeShaderStageObject::UpdatePsoDesc(D3D12_COMPUTE_PIPELINE_STATE_DESC& pso_desc) const
	{
		pso_desc.CS.pShaderBytecode = shader_code_.data();
		pso_desc.CS.BytecodeLength = static_cast<UINT>(shader_code_.size());
	}

	void D3D12ComputeShaderStageObject::StageSpecificCreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids)
	{
		KFL_UNUSED(effect);
		KFL_UNUSED(shader_desc_ids);

		if (!is_available_)
		{
			is_validate_ = false;
		}
	}

	void D3D12ComputeShaderStageObject::StageSpecificStreamIn(std::istream& native_shader_stream)
	{
		native_shader_stream.read(reinterpret_cast<char*>(&cs_block_size_x_), sizeof(cs_block_size_x_));
		cs_block_size_x_ = LE2Native(cs_block_size_x_);

		native_shader_stream.read(reinterpret_cast<char*>(&cs_block_size_y_), sizeof(cs_block_size_y_));
		cs_block_size_y_ = LE2Native(cs_block_size_y_);

		native_shader_stream.read(reinterpret_cast<char*>(&cs_block_size_z_), sizeof(cs_block_size_z_));
		cs_block_size_z_ = LE2Native(cs_block_size_z_);
	}

	void D3D12ComputeShaderStageObject::StageSpecificStreamOut(std::ostream& os)
	{
		uint32_t cs_block_size_x = Native2LE(cs_block_size_x_);
		os.write(reinterpret_cast<char const*>(&cs_block_size_x), sizeof(cs_block_size_x));

		uint32_t cs_block_size_y = Native2LE(cs_block_size_y_);
		os.write(reinterpret_cast<char const*>(&cs_block_size_y), sizeof(cs_block_size_y));

		uint32_t cs_block_size_z = Native2LE(cs_block_size_z_);
		os.write(reinterpret_cast<char const*>(&cs_block_size_z), sizeof(cs_block_size_z));
	}

	void D3D12ComputeShaderStageObject::StageSpecificReflection(ID3D12ShaderReflection* reflection)
	{
		reflection->GetThreadGroupSize(&cs_block_size_x_, &cs_block_size_y_, &cs_block_size_z_);
	}


	D3D12HullShaderStageObject::D3D12HullShaderStageObject() : D3D12ShaderStageObject(ShaderObject::ST_HullShader)
	{
		auto const& re = *checked_cast<D3D12RenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		auto const& caps = re.DeviceCaps();
		is_available_ = caps.hs_support;
	}

	void D3D12HullShaderStageObject::UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const
	{
		pso_desc.HS.pShaderBytecode = shader_code_.data();
		pso_desc.HS.BytecodeLength = static_cast<UINT>(shader_code_.size());
	}

	void D3D12HullShaderStageObject::StageSpecificCreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids)
	{
		KFL_UNUSED(effect);
		KFL_UNUSED(shader_desc_ids);

		if (!is_available_)
		{
			is_validate_ = false;
		}
	}


	D3D12DomainShaderStageObject::D3D12DomainShaderStageObject() : D3D12ShaderStageObject(ShaderObject::ST_DomainShader)
	{
		auto const& re = *checked_cast<D3D12RenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		auto const& caps = re.DeviceCaps();
		is_available_ = caps.ds_support;
	}

	void D3D12DomainShaderStageObject::UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc) const
	{
		pso_desc.DS.pShaderBytecode = shader_code_.data();
		pso_desc.DS.BytecodeLength = static_cast<UINT>(shader_code_.size());

		pso_desc.StreamOutput.pSODeclaration = so_decl_.data();
		pso_desc.StreamOutput.NumEntries = static_cast<UINT>(so_decl_.size());
		pso_desc.StreamOutput.pBufferStrides = nullptr;
		pso_desc.StreamOutput.NumStrides = 0;
		pso_desc.StreamOutput.RasterizedStream = rasterized_stream_;
	}

	void D3D12DomainShaderStageObject::StageSpecificCreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, ShaderObject::ST_NumShaderTypes> const& shader_desc_ids)
	{
		if (is_available_)
		{
			auto const& sd = effect.GetShaderDesc(shader_desc_ids[stage_]);
			if (!sd.so_decl.empty())
			{
				auto const& re =
					*checked_cast<D3D12RenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
				auto const& caps = re.DeviceCaps();
				if (caps.gs_support)
				{
					so_decl_.resize(sd.so_decl.size());
					for (size_t i = 0; i < sd.so_decl.size(); ++i)
					{
						so_decl_[i] = D3D12Mapping::Mapping(sd.so_decl[i]);
					}

					rasterized_stream_ = 0;
					if (effect.GetShaderDesc(shader_desc_ids[ShaderObject::ST_PixelShader]).func_name.empty())
					{
						rasterized_stream_ = D3D12_SO_NO_RASTERIZED_STREAM;
					}
				}
				else
				{
					is_validate_ = false;
				}
			}
		}
		else
		{
			is_validate_ = false;
		}
	}


	D3D12ShaderObject::D3D12ShaderObject() : D3D12ShaderObject(MakeSharedPtr<D3D12ShaderObjectTemplate>())
	{
	}

	D3D12ShaderObject::D3D12ShaderObject(std::shared_ptr<D3D12ShaderObjectTemplate> const & so_template)
		: so_template_(so_template)
	{
		has_discard_ = true;
		has_tessellation_ = false;
		is_shader_validate_.fill(true);
	}

	bool D3D12ShaderObject::AttachNativeShader(ShaderType type, RenderEffect const & effect,
		std::array<uint32_t, ST_NumShaderTypes> const & shader_desc_ids, std::vector<uint8_t> const & native_shader_block)
	{
		bool ret = false;

		this->CreateShaderStage(type);
		
		auto* shader_stage = so_template_->shader_stages_[type].get();
		shader_stage->StreamIn(effect, shader_desc_ids, native_shader_block);
		is_shader_validate_[type] = shader_stage->Validate();
		if (is_shader_validate_[type])
		{
			this->CreateHwResources(type, effect);
			ret = is_shader_validate_[type];
		}

		if (type == ShaderObject::ST_ComputeShader)
		{
			auto const* cs_shader_stage = checked_cast<D3D12ComputeShaderStageObject*>(shader_stage);
			cs_block_size_x_ = cs_shader_stage->CsBlockSizeX();
			cs_block_size_y_ = cs_shader_stage->CsBlockSizeX();
			cs_block_size_z_ = cs_shader_stage->CsBlockSizeX();
		}

		return ret;
	}

	bool D3D12ShaderObject::StreamIn(ResIdentifierPtr const & res, ShaderType type, RenderEffect const & effect,
		std::array<uint32_t, ST_NumShaderTypes> const & shader_desc_ids)
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

	void D3D12ShaderObject::StreamOut(std::ostream& os, ShaderType type)
	{
		so_template_->shader_stages_[type]->StreamOut(os);
	}

	void D3D12ShaderObject::CreateShaderStage(ShaderType stage)
	{
		std::shared_ptr<D3D12ShaderStageObject> shader_stage;
		switch (stage)
		{
		case ShaderObject::ST_VertexShader:
			shader_stage = MakeSharedPtr<D3D12VertexShaderStageObject>();
			break;

		case ShaderObject::ST_PixelShader:
			shader_stage = MakeSharedPtr<D3D12PixelShaderStageObject>();
			break;

		case ShaderObject::ST_GeometryShader:
			shader_stage = MakeSharedPtr<D3D12GeometryShaderStageObject>();
			break;

		case ShaderObject::ST_ComputeShader:
			shader_stage = MakeSharedPtr<D3D12ComputeShaderStageObject>();
			break;

		case ShaderObject::ST_HullShader:
			shader_stage = MakeSharedPtr<D3D12HullShaderStageObject>();
			break;

		case ShaderObject::ST_DomainShader:
			shader_stage = MakeSharedPtr<D3D12DomainShaderStageObject>();
			break;

		default:
			KFL_UNREACHABLE("Invalid shader stage");
		}
		so_template_->shader_stages_[stage] = shader_stage;
	}

	void D3D12ShaderObject::CreateHwResources(ShaderType stage, RenderEffect const& effect)
	{
		auto* shader_stage = so_template_->shader_stages_[stage].get();
		if (!shader_stage->ShaderCodeBlob().empty())
		{
			is_shader_validate_[stage] = shader_stage->Validate();
			if (is_shader_validate_[stage] && ((stage == ST_HullShader) || (stage == ST_DomainShader)))
			{
				has_tessellation_ = true;
			}

			auto const & shader_desc = shader_stage->GetD3D12ShaderDesc();

			d3d_cbuffs_[stage].resize(shader_desc.cb_desc.size());
			samplers_[stage].resize(shader_desc.num_samplers);
			srvsrcs_[stage].resize(shader_desc.num_srvs, std::make_tuple(static_cast<D3D12Resource*>(nullptr), 0, 0));
			srvs_[stage].resize(shader_desc.num_srvs);
			uavsrcs_[stage].resize(shader_desc.num_uavs, nullptr);
			uavs_[stage].resize(shader_desc.num_uavs);

			for (size_t i = 0; i < shader_desc.res_desc.size(); ++i)
			{
				RenderEffectParameter* p = effect.ParameterByName(shader_desc.res_desc[i].name);
				BOOST_ASSERT(p);

				uint32_t offset = shader_desc.res_desc[i].bind_point;
				if (D3D_SIT_SAMPLER == shader_desc.res_desc[i].type)
				{
					SamplerStateObjectPtr sampler;
					p->Value(sampler);
					if (sampler)
					{
						samplers_[stage][offset] = checked_cast<D3D12SamplerStateObject*>(sampler.get())->D3DDesc();
					}
				}
				else
				{
					param_binds_[stage].push_back(this->GetBindFunc(stage, offset, p));
				}
			}
		}
		else
		{
			is_shader_validate_[stage] = false;
		}
	}

	void D3D12ShaderObject::AttachShader(ShaderType type, RenderEffect const & effect,
			RenderTechnique const & tech, RenderPass const & pass, std::array<uint32_t, ST_NumShaderTypes> const & shader_desc_ids)
	{
		this->CreateShaderStage(type);

		auto* shader_stage = so_template_->shader_stages_[type].get();
		shader_stage->AttachShader(effect, tech, pass, shader_desc_ids);
		this->CreateHwResources(type, effect);
	}

	void D3D12ShaderObject::AttachShader(ShaderType type, RenderEffect const & /*effect*/,
			RenderTechnique const & /*tech*/, RenderPass const & /*pass*/, ShaderObjectPtr const & shared_so)
	{
		if (shared_so)
		{
			D3D12ShaderObject const & so = *checked_cast<D3D12ShaderObject*>(shared_so.get());

			is_shader_validate_[type] = so.is_shader_validate_[type];
			if (is_shader_validate_[type])
			{
				so_template_->shader_stages_[type] = so.so_template_->shader_stages_[type];
				switch (type)
				{
				case ST_VertexShader:
				case ST_PixelShader:
				case ST_GeometryShader:
					break;

				case ST_ComputeShader:
					cs_block_size_x_ = so.cs_block_size_x_;
					cs_block_size_y_ = so.cs_block_size_y_;
					cs_block_size_z_ = so.cs_block_size_z_;
					break;

				case ST_HullShader:
				case ST_DomainShader:
					if (so_template_->shader_stages_[type])
					{
						has_tessellation_ = true;
					}
					break;

				default:
					KFL_UNREACHABLE("Invalid shader stage");
				}

				samplers_[type] = so.samplers_[type];
				srvsrcs_[type].resize(so.srvs_[type].size(), std::make_tuple(static_cast<D3D12Resource*>(nullptr), 0, 0));
				srvs_[type].resize(so.srvs_[type].size());
				uavsrcs_[type].resize(so.uavs_[type].size(), nullptr);
				uavs_[type].resize(so.uavs_[type].size());

				d3d_cbuffs_[type].resize(so.d3d_cbuffs_[type].size());

				param_binds_[type].reserve(so.param_binds_[type].size());
				for (auto const & pb : so.param_binds_[type])
				{
					param_binds_[type].push_back(this->GetBindFunc(type, pb.offset, pb.param));
				}
			}
		}
	}

	void D3D12ShaderObject::LinkShaders(RenderEffect const & effect)
	{
		std::vector<uint32_t> all_cbuff_indices;
		is_validate_ = true;
		for (size_t type = 0; type < ST_NumShaderTypes; ++ type)
		{
			is_validate_ &= is_shader_validate_[type];

			auto const* shader_stage = so_template_->shader_stages_[type].get();
			if (shader_stage && !shader_stage->CBufferIndices().empty())
			{
				auto const& shader_desc = shader_stage->GetD3D12ShaderDesc();
				auto const& cbuff_indices = shader_stage->CBufferIndices();

				all_cbuff_indices.insert(all_cbuff_indices.end(), cbuff_indices.begin(), cbuff_indices.end());
				for (size_t i = 0; i < cbuff_indices.size(); ++i)
				{
					auto cbuff = effect.CBufferByIndex(cbuff_indices[i]);
					cbuff->Resize(shader_desc.cb_desc[i].size);
					BOOST_ASSERT(cbuff->NumParameters() == shader_desc.cb_desc[i].var_desc.size());
					for (uint32_t j = 0; j < cbuff->NumParameters(); ++j)
					{
						RenderEffectParameter* param = effect.ParameterByIndex(cbuff->ParameterIndex(j));
						uint32_t stride;
						if (shader_desc.cb_desc[i].var_desc[j].elements > 0)
						{
							if (param->Type() != REDT_float4x4)
							{
								stride = 16;
							}
							else
							{
								stride = 64;
							}
						}
						else
						{
							if (param->Type() != REDT_float4x4)
							{
								stride = 4;
							}
							else
							{
								stride = 16;
							}
						}
						param->BindToCBuffer(*cbuff, shader_desc.cb_desc[i].var_desc[j].start_offset, stride);
					}

					d3d_cbuffs_[type][i] = cbuff->HWBuff().get();
				}
			}
		}

		std::sort(all_cbuff_indices.begin(), all_cbuff_indices.end());
		all_cbuff_indices.erase(std::unique(all_cbuff_indices.begin(), all_cbuff_indices.end()), all_cbuff_indices.end());
		all_cbuffs_.resize(all_cbuff_indices.size());
		for (size_t i = 0; i < all_cbuff_indices.size(); ++i)
		{
			all_cbuffs_[i] = effect.CBufferByIndex(all_cbuff_indices[i]);
		}

		this->CreateRootSignature();

		num_handles_ = 0;
		for (uint32_t i = 0; i < ShaderObject::ST_NumShaderTypes; ++ i)
		{
			num_handles_ += static_cast<uint32_t>(srvs_[i].size() + uavs_[i].size());
		}
	}

	void D3D12ShaderObject::CreateRootSignature()
	{
		D3D12RenderEngine& re = *checked_cast<D3D12RenderEngine*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		ID3D12Device* device = re.D3DDevice();

		std::array<uint32_t, ShaderObject::ST_NumShaderTypes * 4> num;
		uint32_t num_sampler = 0;
		for (uint32_t i = 0; i < ST_NumShaderTypes; ++ i)
		{
			num[i * 4 + 0] = static_cast<uint32_t>(d3d_cbuffs_[i].size());
			num[i * 4 + 1] = static_cast<uint32_t>(srvs_[i].size());
			num[i * 4 + 2] = static_cast<uint32_t>(uavs_[i].size());
			num[i * 4 + 3] = static_cast<uint32_t>(samplers_[i].size());

			num_sampler += num[i * 4 + 3];
		}

		bool has_stream_output = false;
		if (so_template_->shader_stages_[ST_GeometryShader] && so_template_->shader_stages_[ST_GeometryShader]->HasStreamOutput())
		{
			has_stream_output = true;
		}
		else if (so_template_->shader_stages_[ST_DomainShader] && so_template_->shader_stages_[ST_DomainShader]->HasStreamOutput())
		{
			has_stream_output = true;
		}
		else if (so_template_->shader_stages_[ST_VertexShader] && so_template_->shader_stages_[ST_VertexShader]->HasStreamOutput())
		{
			has_stream_output = true;
		}

		so_template_->root_signature_ = re.CreateRootSignature(num, !!so_template_->shader_stages_[ST_VertexShader], has_stream_output);

		if (num_sampler > 0)
		{
			D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc;
			sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			sampler_heap_desc.NumDescriptors = static_cast<UINT>(num_sampler);
			sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			sampler_heap_desc.NodeMask = 0;
			ID3D12DescriptorHeap* s_heap;
			TIFHR(device->CreateDescriptorHeap(&sampler_heap_desc, IID_ID3D12DescriptorHeap, reinterpret_cast<void**>(&s_heap)));
			so_template_->sampler_heap_ = MakeCOMPtr(s_heap);

			UINT const sampler_desc_size = re.SamplerDescSize();
			D3D12_CPU_DESCRIPTOR_HANDLE cpu_sampler_handle = so_template_->sampler_heap_->GetCPUDescriptorHandleForHeapStart();
			for (uint32_t i = 0; i < ST_NumShaderTypes; ++ i)
			{
				if (!samplers_[i].empty())
				{
					for (uint32_t j = 0; j < samplers_[i].size(); ++ j)
					{
						device->CreateSampler(&samplers_[i][j], cpu_sampler_handle);
						cpu_sampler_handle.ptr += sampler_desc_size;
					}
				}
			}
		}
	}
	
	ShaderObjectPtr D3D12ShaderObject::Clone(RenderEffect const & effect)
	{
		D3D12ShaderObjectPtr ret = MakeSharedPtr<D3D12ShaderObject>();
		ret->has_discard_ = has_discard_;
		ret->has_tessellation_ = has_tessellation_;
		ret->is_validate_ = is_validate_;
		ret->is_shader_validate_ = is_shader_validate_;
		ret->cs_block_size_x_ = cs_block_size_x_;
		ret->cs_block_size_y_ = cs_block_size_y_;
		ret->cs_block_size_z_ = cs_block_size_z_;
		ret->so_template_ = so_template_;

		std::vector<uint32_t> all_cbuff_indices;
		for (size_t i = 0; i < ST_NumShaderTypes; ++ i)
		{
			ret->samplers_[i] = samplers_[i];
			ret->srvsrcs_[i].resize(srvsrcs_[i].size(),
				std::make_tuple(static_cast<D3D12Resource*>(nullptr), 0, 0));
			ret->srvs_[i].resize(srvs_[i].size());
			ret->uavsrcs_[i].resize(uavsrcs_[i].size(), nullptr);
			ret->uavs_[i].resize(uavs_[i].size());

			auto const* shader_stage = so_template_->shader_stages_[i].get();
			if (shader_stage && !shader_stage->CBufferIndices().empty())
			{
				auto const& cbuff_indices = shader_stage->CBufferIndices();

				ret->d3d_cbuffs_[i].resize(d3d_cbuffs_[i].size());
				all_cbuff_indices.insert(all_cbuff_indices.end(), cbuff_indices.begin(), cbuff_indices.end());
				for (size_t j = 0; j < cbuff_indices.size(); ++ j)
				{
					auto cbuff = effect.CBufferByIndex(cbuff_indices[j]);
					ret->d3d_cbuffs_[i][j] = cbuff->HWBuff().get();
				}
			}

			ret->param_binds_[i].reserve(param_binds_[i].size());
			for (auto const & pb : param_binds_[i])
			{
				ret->param_binds_[i].push_back(ret->GetBindFunc(static_cast<ShaderType>(i), pb.offset,
					effect.ParameterByName(pb.param->Name())));
			}
		}

		std::sort(all_cbuff_indices.begin(), all_cbuff_indices.end());
		all_cbuff_indices.erase(std::unique(all_cbuff_indices.begin(), all_cbuff_indices.end()),
			all_cbuff_indices.end());
		ret->all_cbuffs_.resize(all_cbuff_indices.size());
		for (size_t i = 0; i < all_cbuff_indices.size(); ++ i)
		{
			ret->all_cbuffs_[i] = effect.CBufferByIndex(all_cbuff_indices[i]);
		}

		ret->num_handles_ = num_handles_;

		return ret;
	}

	D3D12ShaderObject::ParameterBind D3D12ShaderObject::GetBindFunc(ShaderType type, uint32_t offset, RenderEffectParameter* param)
	{
		ParameterBind ret;
		ret.param = param;
		ret.offset = offset;

		switch (param->Type())
		{
		case REDT_bool:
		case REDT_uint:
		case REDT_int:
		case REDT_float:
		case REDT_uint2:
		case REDT_uint3:
		case REDT_uint4:
		case REDT_int2:
		case REDT_int3:
		case REDT_int4:
		case REDT_float2:
		case REDT_float3:
		case REDT_float4:
		case REDT_float4x4:
		case REDT_sampler:
			break;

		case REDT_texture1D:
		case REDT_texture2D:
		case REDT_texture2DMS:
		case REDT_texture3D:
		case REDT_textureCUBE:
		case REDT_texture1DArray:
		case REDT_texture2DArray:
		case REDT_texture2DMSArray:
		case REDT_texture3DArray:
		case REDT_textureCUBEArray:
			ret.func = SetD3D12ShaderParameterTextureSRV(srvsrcs_[type][offset], srvs_[type][offset], param);
			break;

		case REDT_buffer:
		case REDT_structured_buffer:
		case REDT_consume_structured_buffer:
		case REDT_append_structured_buffer:
		case REDT_byte_address_buffer:
			ret.func = SetD3D12ShaderParameterGraphicsBufferSRV(srvsrcs_[type][offset], srvs_[type][offset], param);
			break;

		case REDT_rw_texture1D:
		case REDT_rw_texture2D:
		case REDT_rw_texture3D:
		case REDT_rw_texture1DArray:
		case REDT_rw_texture2DArray:
		case REDT_rasterizer_ordered_texture1D:
		case REDT_rasterizer_ordered_texture1DArray:
		case REDT_rasterizer_ordered_texture2D:
		case REDT_rasterizer_ordered_texture2DArray:
		case REDT_rasterizer_ordered_texture3D:
			ret.func = SetD3D12ShaderParameterTextureUAV(uavsrcs_[type][offset], uavs_[type][offset], param);
			break;

		case REDT_rw_buffer:
		case REDT_rw_structured_buffer:
		case REDT_rw_byte_address_buffer:
		case REDT_rasterizer_ordered_buffer:
		case REDT_rasterizer_ordered_structured_buffer:
		case REDT_rasterizer_ordered_byte_address_buffer:
			ret.func = SetD3D12ShaderParameterGraphicsBufferUAV(uavsrcs_[type][offset], uavs_[type][offset], param);
			break;

		default:
			KFL_UNREACHABLE("Can't be called");
		}

		return ret;
	}

	void D3D12ShaderObject::Bind()
	{
		auto& re = *checked_cast<D3D12RenderEngine*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
		auto* cmd_list = re.D3DRenderCmdList();

		for (size_t st = 0; st < ST_NumShaderTypes; ++ st)
		{
			for (auto const & pb : param_binds_[st])
			{
				pb.func();
			}

			D3D12_RESOURCE_STATES state_after
				= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			for (auto const & srvsrc : srvsrcs_[st])
			{
				for (uint32_t subres = 0; subres < std::get<2>(srvsrc); ++ subres)
				{
					auto res = std::get<0>(srvsrc);
					if (res != nullptr)
					{
						res->UpdateResourceBarrier(cmd_list, std::get<1>(srvsrc) + subres, state_after);
					}
				}
			}

			state_after = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			for (auto const & uavsrc : uavsrcs_[st])
			{
				if (uavsrc != nullptr)
				{
#ifdef KLAYGE_DEBUG
					for (auto const & srvsrc : srvsrcs_[st])
					{
						BOOST_ASSERT(std::get<0>(srvsrc) != uavsrc);
					}
#endif

					uavsrc->UpdateResourceBarrier(cmd_list, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state_after);
				}
			}
		}
		
		re.FlushResourceBarriers(cmd_list);

		for (auto cb : all_cbuffs_)
		{
			cb->Update();
		}
	}

	void D3D12ShaderObject::Unbind()
	{
	}

	void D3D12ShaderObject::UpdatePsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc)
	{
		pso_desc.pRootSignature = so_template_->root_signature_.get();

		{
			auto const* ps_stage = so_template_->shader_stages_[ShaderObject::ST_PixelShader].get();
			if (ps_stage)
			{
				ps_stage->UpdatePsoDesc(pso_desc);
			}
			else
			{
				pso_desc.PS.pShaderBytecode = nullptr;
				pso_desc.PS.BytecodeLength = 0;
			}
		}
		{
			auto const* gs_stage = so_template_->shader_stages_[ShaderObject::ST_GeometryShader].get();
			if (gs_stage)
			{
				gs_stage->UpdatePsoDesc(pso_desc);
			}
			else
			{
				pso_desc.GS.pShaderBytecode = nullptr;
				pso_desc.GS.BytecodeLength = 0;
			}
		}
		{
			auto const* ds_stage = so_template_->shader_stages_[ShaderObject::ST_DomainShader].get();
			if (ds_stage)
			{
				ds_stage->UpdatePsoDesc(pso_desc);
			}
			else
			{
				pso_desc.DS.pShaderBytecode = nullptr;
				pso_desc.DS.BytecodeLength = 0;
			}
		}
		{
			auto const* hs_stage = so_template_->shader_stages_[ShaderObject::ST_HullShader].get();
			if (hs_stage)
			{
				hs_stage->UpdatePsoDesc(pso_desc);
			}
			else
			{
				pso_desc.HS.pShaderBytecode = nullptr;
				pso_desc.HS.BytecodeLength = 0;
			}
		}
		{
			auto const* vs_stage = so_template_->shader_stages_[ShaderObject::ST_VertexShader].get();
			if (vs_stage)
			{
				vs_stage->UpdatePsoDesc(pso_desc);
			}
			else
			{
				pso_desc.VS.pShaderBytecode = nullptr;
				pso_desc.VS.BytecodeLength = 0;
			}
		}
	}

	void D3D12ShaderObject::UpdatePsoDesc(D3D12_COMPUTE_PIPELINE_STATE_DESC& pso_desc)
	{
		pso_desc.pRootSignature = so_template_->root_signature_.get();

		auto const* cs_stage = so_template_->shader_stages_[ShaderObject::ST_ComputeShader].get();
		if (cs_stage)
		{
			cs_stage->UpdatePsoDesc(pso_desc);
		}
		else
		{
			pso_desc.CS.pShaderBytecode = nullptr;
			pso_desc.CS.BytecodeLength = 0;
		}
	}
}
