// D3D11ShaderObject.cpp
// KlayGE D3D11 shader对象类 实现文件
// Ver 3.9.0
// 版权所有(C) 龚敏敏, 2009
// Homepage: http://www.klayge.org
//
// 3.9.0
// 加速Shader编译 (2009.7.31)
//
// 3.8.0
// 支持Gemoetry Shader (2009.2.5)
// 初次建立 (2009.1.30)
//
// 修改记录
/////////////////////////////////////////////////////////////////////////////////

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

#if KLAYGE_IS_DEV_PLATFORM
#include <KlayGE/SALWrapper.hpp>
#include <d3dcompiler.h>
#endif

#include <KlayGE/D3D11/D3D11RenderEngine.hpp>
#include <KlayGE/D3D11/D3D11RenderStateObject.hpp>
#include <KlayGE/D3D11/D3D11Mapping.hpp>
#include <KlayGE/D3D11/D3D11Texture.hpp>
#include <KlayGE/D3D11/D3D11ShaderObject.hpp>

namespace
{
	using namespace KlayGE;

	class SetD3D11ShaderParameterTextureSRV
	{
	public:
		SetD3D11ShaderParameterTextureSRV(std::tuple<void*, uint32_t, uint32_t>& srvsrc,
				ID3D11ShaderResourceView*& srv, RenderEffectParameter* param)
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
					*srvsrc_ = std::make_tuple(srv->TextureResource().get(),
						srv->FirstArrayIndex() * srv->TextureResource()->NumMipMaps() + srv->FirstLevel(),
						srv->ArraySize() * srv->NumLevels());
				}
				else
				{
					std::get<0>(*srvsrc_) = nullptr;
				}
				*srv_ = checked_cast<D3D11ShaderResourceView*>(srv.get())->RetrieveD3DShaderResourceView();
			}
			else
			{
				std::get<0>(*srvsrc_) = nullptr;
				*srv_ = nullptr;
			}
		}

	private:
		std::tuple<void*, uint32_t, uint32_t>* srvsrc_;
		ID3D11ShaderResourceView** srv_;
		RenderEffectParameter* param_;
	};

	class SetD3D11ShaderParameterGraphicsBufferSRV
	{
	public:
		SetD3D11ShaderParameterGraphicsBufferSRV(std::tuple<void*, uint32_t, uint32_t>& srvsrc,
				ID3D11ShaderResourceView*& srv, RenderEffectParameter* param)
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
					*srvsrc_ = std::make_tuple(srv->BufferResource().get(), 0, 1);
				}
				else
				{
					std::get<0>(*srvsrc_) = nullptr;
				}
				*srv_ = checked_cast<D3D11ShaderResourceView*>(srv.get())->RetrieveD3DShaderResourceView();
			}
			else
			{
				std::get<0>(*srvsrc_) = nullptr;
				*srv_ = nullptr;
			}
		}

	private:
		std::tuple<void*, uint32_t, uint32_t>* srvsrc_;
		ID3D11ShaderResourceView** srv_;
		RenderEffectParameter* param_;
	};

	class SetD3D11ShaderParameterTextureUAV
	{
	public:
		SetD3D11ShaderParameterTextureUAV(void*& uavsrc, ID3D11UnorderedAccessView*& uav, RenderEffectParameter* param)
			: uavsrc_(&uavsrc), uav_(&uav), param_(param)
		{
		}

		void operator()()
		{
			UnorderedAccessViewPtr uav;
			param_->Value(uav);
			if (uav)
			{
				*uavsrc_ = uav->TextureResource().get();
				*uav_ = checked_cast<D3D11UnorderedAccessView*>(uav.get())->RetrieveD3DUnorderedAccessView();
			}
			else
			{
				*uavsrc_ = nullptr;
			}
		}

	private:
		void** uavsrc_;
		ID3D11UnorderedAccessView** uav_;
		RenderEffectParameter* param_;
	};

	class SetD3D11ShaderParameterGraphicsBufferUAV
	{
	public:
		SetD3D11ShaderParameterGraphicsBufferUAV(void*& uavsrc, ID3D11UnorderedAccessView*& uav, RenderEffectParameter* param)
			: uavsrc_(&uavsrc), uav_(&uav), param_(param)
		{
		}

		void operator()()
		{
			UnorderedAccessViewPtr uav;
			param_->Value(uav);
			if (uav)
			{
				*uavsrc_ = uav->BufferResource().get();
				*uav_ = checked_cast<D3D11UnorderedAccessView*>(uav.get())->RetrieveD3DUnorderedAccessView();
			}
			else
			{
				*uavsrc_ = nullptr;
			}
		}

	private:
		void** uavsrc_;
		ID3D11UnorderedAccessView** uav_;
		RenderEffectParameter* param_;
	};
}

namespace KlayGE
{
	D3D11ShaderStageObject::D3D11ShaderStageObject(ShaderStage stage) : ShaderStageObject(stage)
	{
	}

	void D3D11ShaderStageObject::StreamIn(RenderEffect const& effect,
		std::array<uint32_t, NumShaderStages> const& shader_desc_ids, std::vector<uint8_t> const& native_shader_block)
	{
		is_validate_ = false;
		std::string_view const shader_profile = this->GetShaderProfile(effect, shader_desc_ids[static_cast<uint32_t>(stage_)]);
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

					native_shader_stream.read(
						reinterpret_cast<char*>(&shader_desc_.res_desc[i].type), sizeof(shader_desc_.res_desc[i].type));

					native_shader_stream.read(
						reinterpret_cast<char*>(&shader_desc_.res_desc[i].dimension), sizeof(shader_desc_.res_desc[i].dimension));

					native_shader_stream.read(
						reinterpret_cast<char*>(&shader_desc_.res_desc[i].bind_point), sizeof(shader_desc_.res_desc[i].bind_point));
					shader_desc_.res_desc[i].bind_point = LE2Native(shader_desc_.res_desc[i].bind_point);
				}

				this->FillCBufferIndices(effect);

				this->StageSpecificStreamIn(native_shader_stream);

				this->CreateHwShader(effect, shader_desc_ids);
			}
		}
	}

	void D3D11ShaderStageObject::StreamOut(std::ostream& os)
	{
		std::vector<char> native_shader_block;
		VectorOutputStreamBuf native_shader_buff(native_shader_block);
		std::ostream oss(&native_shader_buff);

		{
			uint8_t len = static_cast<uint8_t>(shader_profile_.size());
			oss.write(reinterpret_cast<char const*>(&len), sizeof(len));
			oss.write(reinterpret_cast<char const*>(shader_profile_.data()), len);
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
			os.write(reinterpret_cast<char const*>(&tmp), sizeof(tmp));
		}
		if (len > 0)
		{
			os.write(reinterpret_cast<char const*>(&native_shader_block[0]), len * sizeof(native_shader_block[0]));
		}
	}

	void D3D11ShaderStageObject::AttachShader(RenderEffect const& effect, RenderTechnique const& tech, RenderPass const& pass,
		std::array<uint32_t, NumShaderStages> const& shader_desc_ids)
	{
		shader_code_.clear();

#ifdef KLAYGE_PLATFORM_WINDOWS_DESKTOP
		uint32_t const shader_desc_id = shader_desc_ids[static_cast<uint32_t>(stage_)];

		auto const& sd = effect.GetShaderDesc(shader_desc_id);

		shader_profile_ = std::string(this->GetShaderProfile(effect, shader_desc_id));
		is_validate_ = !shader_profile_.empty();

		if (is_validate_)
		{
			std::vector<std::pair<char const*, char const*>> macros;
			macros.emplace_back("KLAYGE_D3D11", "1");
			macros.emplace_back("KLAYGE_FRAG_DEPTH", "1");

			uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if !defined(KLAYGE_DEBUG)
			flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
			shader_code_ =
				ShaderObject::CompileToDXBC(stage_, effect, tech, pass, macros, sd.func_name.c_str(), shader_profile_.c_str(), flags);

			if (!shader_code_.empty())
			{
				ID3D11ShaderReflection* reflection;
				ShaderObject::ReflectDXBC(shader_code_, reinterpret_cast<void**>(&reflection));
				if (reflection != nullptr)
				{
					D3D11_SHADER_DESC desc;
					reflection->GetDesc(&desc);

					for (UINT c = 0; c < desc.ConstantBuffers; ++c)
					{
						ID3D11ShaderReflectionConstantBuffer* reflection_cb = reflection->GetConstantBufferByIndex(c);

						D3D11_SHADER_BUFFER_DESC d3d_cb_desc;
						reflection_cb->GetDesc(&d3d_cb_desc);
						if ((D3D_CT_CBUFFER == d3d_cb_desc.Type) || (D3D_CT_TBUFFER == d3d_cb_desc.Type))
						{
							D3D11ShaderDesc::ConstantBufferDesc cb_desc;
							cb_desc.name = d3d_cb_desc.Name;
							cb_desc.name_hash = RT_HASH(d3d_cb_desc.Name);
							cb_desc.size = d3d_cb_desc.Size;

							for (UINT v = 0; v < d3d_cb_desc.Variables; ++v)
							{
								ID3D11ShaderReflectionVariable* reflection_var = reflection_cb->GetVariableByIndex(v);

								D3D11_SHADER_VARIABLE_DESC var_desc;
								reflection_var->GetDesc(&var_desc);

								D3D11_SHADER_TYPE_DESC type_desc;
								reflection_var->GetType()->GetDesc(&type_desc);

								D3D11ShaderDesc::ConstantBufferDesc::VariableDesc vd;
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
						D3D11_SHADER_INPUT_BIND_DESC si_desc;
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
						D3D11_SHADER_INPUT_BIND_DESC si_desc;
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
								D3D11ShaderDesc::BoundResourceDesc brd;
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
		KFL_UNUSED(effect);
		KFL_UNUSED(tech);
		KFL_UNUSED(pass);
		KFL_UNUSED(shader_desc_ids);
#endif

		this->CreateHwShader(effect, shader_desc_ids);
	}

	void D3D11ShaderStageObject::CreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, NumShaderStages> const& shader_desc_ids)
	{
		if (!shader_code_.empty())
		{
			auto const& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
			auto const& caps = re.DeviceCaps();

			ShaderDesc const& sd = effect.GetShaderDesc(shader_desc_ids[static_cast<uint32_t>(stage_)]);

			uint8_t const shader_major_ver = ("auto" == sd.profile) ? 0 : static_cast<uint8_t>(sd.profile[3] - '0');
			uint8_t const shader_minor_ver = ("auto" == sd.profile) ? 0 : static_cast<uint8_t>(sd.profile[5] - '0');
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
			this->ClearHwShader();
		}
	}

	void D3D11ShaderStageObject::FillCBufferIndices(RenderEffect const& effect)
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

	ID3D11GeometryShaderPtr D3D11ShaderStageObject::CreateGeometryShaderWithStreamOutput(RenderEffect const& effect,
		std::array<uint32_t, NumShaderStages> const& shader_desc_ids, ArrayRef<uint8_t> code_blob,
		std::vector<ShaderDesc::StreamOutputDecl> const& so_decl)
	{
		BOOST_ASSERT(!code_blob.empty());

		auto& rf = Context::Instance().RenderFactoryInstance();
		auto const& d3d11_re = *checked_cast<D3D11RenderEngine const*>(&rf.RenderEngineInstance());
		auto d3d_device = d3d11_re.D3DDevice();
		auto const& caps = d3d11_re.DeviceCaps();

		std::vector<D3D11_SO_DECLARATION_ENTRY> d3d11_decl(so_decl.size());
		for (size_t i = 0; i < so_decl.size(); ++i)
		{
			d3d11_decl[i] = D3D11Mapping::Mapping(so_decl[i]);
		}

		UINT rasterized_stream = 0;
		if ((caps.max_shader_model >= ShaderModel(5, 0)) &&
			(effect.GetShaderDesc(shader_desc_ids[static_cast<uint32_t>(ShaderStage::PixelShader)]).func_name.empty()))
		{
			rasterized_stream = D3D11_SO_NO_RASTERIZED_STREAM;
		}

		ID3D11GeometryShader* gs;
		if (FAILED(d3d_device->CreateGeometryShaderWithStreamOutput(code_blob.data(), code_blob.size(), &d3d11_decl[0],
				static_cast<UINT>(d3d11_decl.size()), nullptr, 0, rasterized_stream, nullptr, &gs)))
		{
			is_validate_ = false;
		}

		return MakeCOMPtr(gs);
	}

	std::string_view D3D11ShaderStageObject::GetShaderProfile(RenderEffect const& effect, uint32_t shader_desc_id) const
	{
		std::string_view shader_profile = effect.GetShaderDesc(shader_desc_id).profile;
		if (is_available_)
		{
			if (shader_profile == "auto")
			{
				auto const& re =
					*checked_cast<D3D11RenderEngine const*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());
				shader_profile = re.DefaultShaderProfile(stage_);
			}
		}
		else
		{
			shader_profile = std::string_view();
		}
		return shader_profile;
	}


	D3D11VertexShaderStageObject::D3D11VertexShaderStageObject() : D3D11ShaderStageObject(ShaderStage::VertexShader)
	{
		is_available_ = true;
	}

	void D3D11VertexShaderStageObject::ClearHwShader()
	{
		vertex_shader_.reset();
		geometry_shader_.reset();
	}

	void D3D11VertexShaderStageObject::StageSpecificCreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, NumShaderStages> const& shader_desc_ids)
	{
		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		D3D11RenderEngine const& re = *checked_cast<D3D11RenderEngine const*>(&rf.RenderEngineInstance());
		ID3D11Device* d3d_device = re.D3DDevice();

		ID3D11VertexShader* vs;
		if (FAILED(d3d_device->CreateVertexShader(shader_code_.data(), shader_code_.size(), nullptr, &vs)))
		{
			is_validate_ = false;
		}
		else
		{
			vertex_shader_ = MakeCOMPtr(vs);

			RenderDeviceCaps const& caps = re.DeviceCaps();
			ShaderDesc const& sd = effect.GetShaderDesc(shader_desc_ids[static_cast<uint32_t>(stage_)]);
			if (!sd.so_decl.empty())
			{
				if (caps.gs_support)
				{
					geometry_shader_ = this->CreateGeometryShaderWithStreamOutput(effect, shader_desc_ids, shader_code_, sd.so_decl);
				}
				else
				{
					is_validate_ = false;
				}
			}
		}
	}

	void D3D11VertexShaderStageObject::StageSpecificStreamIn(std::istream& native_shader_stream)
	{
		native_shader_stream.read(reinterpret_cast<char*>(&vs_signature_), sizeof(vs_signature_));
		vs_signature_ = LE2Native(vs_signature_);
	}

	void D3D11VertexShaderStageObject::StageSpecificStreamOut(std::ostream& os)
	{
		uint32_t const vs_signature = Native2LE(vs_signature_);
		os.write(reinterpret_cast<char const*>(&vs_signature), sizeof(vs_signature));
	}

#if KLAYGE_IS_DEV_PLATFORM
	void D3D11VertexShaderStageObject::StageSpecificReflection(ID3D11ShaderReflection* reflection)
	{
		D3D11_SHADER_DESC desc;
		reflection->GetDesc(&desc);

		vs_signature_ = 0;
		D3D11_SIGNATURE_PARAMETER_DESC signature;
		for (uint32_t i = 0; i < desc.InputParameters; ++i)
		{
			reflection->GetInputParameterDesc(i, &signature);

			size_t seed = RT_HASH(signature.SemanticName);
			HashCombine(seed, signature.SemanticIndex);
			HashCombine(seed, signature.Register);
			HashCombine(seed, static_cast<uint32_t>(signature.SystemValueType));
			HashCombine(seed, static_cast<uint32_t>(signature.ComponentType));
			HashCombine(seed, signature.Mask);
			HashCombine(seed, signature.ReadWriteMask);
			HashCombine(seed, signature.Stream);
			HashCombine(seed, signature.MinPrecision);

			size_t sig = vs_signature_;
			HashCombine(sig, seed);
			vs_signature_ = static_cast<uint32_t>(sig);
		}
	}
#endif


	D3D11PixelShaderStageObject::D3D11PixelShaderStageObject() : D3D11ShaderStageObject(ShaderStage::PixelShader)
	{
		is_available_ = true;
	}

	void D3D11PixelShaderStageObject::ClearHwShader()
	{
		pixel_shader_.reset();
	}

	void D3D11PixelShaderStageObject::StageSpecificCreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, NumShaderStages> const& shader_desc_ids)
	{
		KFL_UNUSED(effect);
		KFL_UNUSED(shader_desc_ids);

		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		D3D11RenderEngine const& re = *checked_cast<D3D11RenderEngine const*>(&rf.RenderEngineInstance());
		ID3D11Device* d3d_device = re.D3DDevice();

		ID3D11PixelShader* ps;
		if (FAILED(d3d_device->CreatePixelShader(shader_code_.data(), shader_code_.size(), nullptr, &ps)))
		{
			is_validate_ = false;
		}
		else
		{
			pixel_shader_ = MakeCOMPtr(ps);
		}
	}


	D3D11GeometryShaderStageObject::D3D11GeometryShaderStageObject() : D3D11ShaderStageObject(ShaderStage::GeometryShader)
	{
		auto const& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
		auto const& caps = re.DeviceCaps();
		is_available_ = caps.gs_support;
	}

	void D3D11GeometryShaderStageObject::ClearHwShader()
	{
		geometry_shader_.reset();
	}

	void D3D11GeometryShaderStageObject::StageSpecificCreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, NumShaderStages> const& shader_desc_ids)
	{
		if (is_available_)
		{
			ShaderDesc const& sd = effect.GetShaderDesc(shader_desc_ids[static_cast<uint32_t>(stage_)]);
			if (sd.so_decl.empty())
			{
				RenderFactory& rf = Context::Instance().RenderFactoryInstance();
				D3D11RenderEngine const& re = *checked_cast<D3D11RenderEngine const*>(&rf.RenderEngineInstance());
				ID3D11Device* d3d_device = re.D3DDevice();
		
				ID3D11GeometryShader* gs;
				if (FAILED(d3d_device->CreateGeometryShader(shader_code_.data(), shader_code_.size(), nullptr, &gs)))
				{
					is_validate_ = false;
				}
				else
				{
					geometry_shader_ = MakeCOMPtr(gs);
				}
			}
			else
			{
				geometry_shader_ = this->CreateGeometryShaderWithStreamOutput(effect, shader_desc_ids, shader_code_, sd.so_decl);
			}
		}
		else
		{
			is_validate_ = false;
		}
	}


	D3D11ComputeShaderStageObject::D3D11ComputeShaderStageObject() : D3D11ShaderStageObject(ShaderStage::ComputeShader)
	{
		auto const& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
		auto const& caps = re.DeviceCaps();
		is_available_ = caps.cs_support;
	}

	void D3D11ComputeShaderStageObject::ClearHwShader()
	{
		compute_shader_.reset();
	}

	void D3D11ComputeShaderStageObject::StageSpecificCreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, NumShaderStages> const& shader_desc_ids)
	{
		KFL_UNUSED(effect);
		KFL_UNUSED(shader_desc_ids);

		if (is_available_)
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();
			D3D11RenderEngine const& re = *checked_cast<D3D11RenderEngine const*>(&rf.RenderEngineInstance());
			ID3D11Device* d3d_device = re.D3DDevice();

			ID3D11ComputeShader* cs;
			if (FAILED(d3d_device->CreateComputeShader(shader_code_.data(), shader_code_.size(), nullptr, &cs)))
			{
				is_validate_ = false;
			}
			else
			{
				compute_shader_ = MakeCOMPtr(cs);
			}
		}
		else
		{
			is_validate_ = false;
		}
	}

	void D3D11ComputeShaderStageObject::StageSpecificStreamIn(std::istream& native_shader_stream)
	{
		native_shader_stream.read(reinterpret_cast<char*>(&block_size_x_), sizeof(block_size_x_));
		block_size_x_ = LE2Native(block_size_x_);

		native_shader_stream.read(reinterpret_cast<char*>(&block_size_y_), sizeof(block_size_y_));
		block_size_y_ = LE2Native(block_size_y_);

		native_shader_stream.read(reinterpret_cast<char*>(&block_size_z_), sizeof(block_size_z_));
		block_size_z_ = LE2Native(block_size_z_);
	}

	void D3D11ComputeShaderStageObject::StageSpecificStreamOut(std::ostream& os)
	{
		uint32_t block_size_x = Native2LE(block_size_x_);
		os.write(reinterpret_cast<char const*>(&block_size_x), sizeof(block_size_x));

		uint32_t block_size_y = Native2LE(block_size_y_);
		os.write(reinterpret_cast<char const*>(&block_size_y), sizeof(block_size_y));

		uint32_t block_size_z = Native2LE(block_size_z_);
		os.write(reinterpret_cast<char const*>(&block_size_z), sizeof(block_size_z));
	}

#if KLAYGE_IS_DEV_PLATFORM
	void D3D11ComputeShaderStageObject::StageSpecificReflection(ID3D11ShaderReflection* reflection)
	{
		reflection->GetThreadGroupSize(&block_size_x_, &block_size_y_, &block_size_z_);
	}
#endif


	D3D11HullShaderStageObject::D3D11HullShaderStageObject() : D3D11ShaderStageObject(ShaderStage::HullShader)
	{
		auto const& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
		auto const& caps = re.DeviceCaps();
		is_available_ = caps.hs_support;
	}

	void D3D11HullShaderStageObject::ClearHwShader()
	{
		hull_shader_.reset();
	}

	void D3D11HullShaderStageObject::StageSpecificCreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, NumShaderStages> const& shader_desc_ids)
	{
		KFL_UNUSED(effect);
		KFL_UNUSED(shader_desc_ids);

		if (is_available_)
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();
			D3D11RenderEngine const& re = *checked_cast<D3D11RenderEngine const*>(&rf.RenderEngineInstance());
			ID3D11Device* d3d_device = re.D3DDevice();

			ID3D11HullShader* hs;
			if (FAILED(d3d_device->CreateHullShader(shader_code_.data(), shader_code_.size(), nullptr, &hs)))
			{
				is_validate_ = false;
			}
			else
			{
				hull_shader_ = MakeCOMPtr(hs);
			}
		}
		else
		{
			is_validate_ = false;
		}
	}


	D3D11DomainShaderStageObject::D3D11DomainShaderStageObject() : D3D11ShaderStageObject(ShaderStage::DomainShader)
	{
		auto const& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
		auto const& caps = re.DeviceCaps();
		is_available_ = caps.ds_support;
	}

	void D3D11DomainShaderStageObject::ClearHwShader()
	{
		domain_shader_.reset();
		geometry_shader_.reset();
	}

	void D3D11DomainShaderStageObject::StageSpecificCreateHwShader(
		RenderEffect const& effect, std::array<uint32_t, NumShaderStages> const& shader_desc_ids)
	{
		if (is_available_)
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();
			D3D11RenderEngine const& re = *checked_cast<D3D11RenderEngine const*>(&rf.RenderEngineInstance());
			ID3D11Device* d3d_device = re.D3DDevice();

			ID3D11DomainShader* ds;
			if (FAILED(d3d_device->CreateDomainShader(shader_code_.data(), shader_code_.size(), nullptr, &ds)))
			{
				is_validate_ = false;
			}
			else
			{
				domain_shader_ = MakeCOMPtr(ds);

				ShaderDesc const& sd = effect.GetShaderDesc(shader_desc_ids[static_cast<uint32_t>(stage_)]);
				if (!sd.so_decl.empty())
				{
					RenderDeviceCaps const& caps = re.DeviceCaps();
					if (caps.gs_support)
					{
						geometry_shader_ = this->CreateGeometryShaderWithStreamOutput(effect, shader_desc_ids, shader_code_, sd.so_decl);
					}
					else
					{
						is_validate_ = false;
					}
				}
			}
		}
		else
		{
			is_validate_ = false;
		}
	}


	D3D11ShaderObject::D3D11ShaderObject() : D3D11ShaderObject(MakeSharedPtr<ShaderObjectTemplate>())
	{
	}

	D3D11ShaderObject::D3D11ShaderObject(std::shared_ptr<ShaderObjectTemplate> const& so_template) : ShaderObject(so_template)
	{
		has_discard_ = true;
	}

	bool D3D11ShaderObject::AttachNativeShader(ShaderStage stage, RenderEffect const& effect,
		std::array<uint32_t, NumShaderStages> const& shader_desc_ids, std::vector<uint8_t> const& native_shader_block)
	{
		auto& rf = Context::Instance().RenderFactoryInstance();
		auto shader_stage = rf.MakeShaderStageObject(stage);

		this->AttachStage(stage, shader_stage);
		shader_stage->StreamIn(effect, shader_desc_ids, native_shader_block);
		if (shader_stage->Validate())
		{
			this->CreateHwResources(stage, effect);
		}

		return shader_stage->Validate();
	}

	void D3D11ShaderObject::CreateHwResources(ShaderStage stage, RenderEffect const& effect)
	{
		auto* shader_stage = checked_cast<D3D11ShaderStageObject*>(this->Stage(stage).get());
		if (!shader_stage->ShaderCodeBlob().empty())
		{
			auto const & shader_desc = shader_stage->GetD3D11ShaderDesc();

			uint32_t const stage_index = static_cast<uint32_t>(stage);

			d3d11_cbuffs_[stage_index].resize(shader_desc.cb_desc.size());
			samplers_[stage_index].resize(shader_desc.num_samplers);
			srvsrcs_[stage_index].resize(shader_desc.num_srvs, std::make_tuple(static_cast<void*>(nullptr), 0, 0));
			srvs_[stage_index].resize(shader_desc.num_srvs);
			uavsrcs_.resize(shader_desc.num_uavs, nullptr);
			uavs_.resize(shader_desc.num_uavs);

			for (size_t i = 0; i < shader_desc.res_desc.size(); ++ i)
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
						samplers_[stage_index][offset] = checked_cast<D3D11SamplerStateObject*>(sampler.get())->D3DSamplerState();
					}
				}
				else
				{
					param_binds_[stage_index].push_back(this->GetBindFunc(stage, offset, p));
				}
			}
		}
	}

	void D3D11ShaderObject::AttachShader(ShaderStage stage, RenderEffect const & effect,
			RenderTechnique const & tech, RenderPass const & pass, std::array<uint32_t, NumShaderStages> const & shader_desc_ids)
	{
		auto& rf = Context::Instance().RenderFactoryInstance();
		auto shader_stage = rf.MakeShaderStageObject(stage);

		this->AttachStage(stage, shader_stage);
		shader_stage->AttachShader(effect, tech, pass, shader_desc_ids);
		this->CreateHwResources(stage, effect);
	}

	void D3D11ShaderObject::AttachShader(ShaderStage stage, RenderEffect const & /*effect*/,
			RenderTechnique const & /*tech*/, RenderPass const & /*pass*/, ShaderObjectPtr const & shared_so)
	{
		if (shared_so)
		{
			D3D11ShaderObject const & so = *checked_cast<D3D11ShaderObject*>(shared_so.get());

			uint32_t const stage_index = static_cast<uint32_t>(stage);

			this->AttachStage(stage, so.Stage(stage));
			if (this->Stage(stage)->Validate())
			{
				samplers_[stage_index] = so.samplers_[stage_index];
				srvsrcs_[stage_index].resize(so.srvs_[stage_index].size(), std::make_tuple(static_cast<void*>(nullptr), 0, 0));
				srvs_[stage_index].resize(so.srvs_[stage_index].size());
				if (checked_cast<D3D11ShaderStageObject*>(so.Stage(stage).get())->GetD3D11ShaderDesc().num_uavs > 0)
				{
					uavsrcs_.resize(so.uavs_.size(), nullptr);
					uavs_.resize(so.uavs_.size());
				}

				d3d11_cbuffs_[stage_index].resize(so.d3d11_cbuffs_[stage_index].size());

				param_binds_[stage_index].reserve(so.param_binds_[stage_index].size());
				for (auto const & pb : so.param_binds_[stage_index])
				{
					param_binds_[stage_index].push_back(this->GetBindFunc(stage, pb.offset, pb.param));
				}
			}
		}
	}

	void D3D11ShaderObject::LinkShaders(RenderEffect const & effect)
	{
		std::vector<uint32_t> all_cbuff_indices;
		is_validate_ = true;
		for (size_t stage = 0; stage < NumShaderStages; ++stage)
		{
			auto const* shader_stage = checked_cast<D3D11ShaderStageObject*>(this->Stage(static_cast<ShaderStage>(stage)).get());
			if (shader_stage)
			{
				is_validate_ &= shader_stage->Validate();

				if (!shader_stage->CBufferIndices().empty())
				{
					auto const& shader_desc = shader_stage->GetD3D11ShaderDesc();
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

						d3d11_cbuffs_[stage][i] = checked_cast<D3D11GraphicsBuffer*>(cbuff->HWBuff().get())->D3DBuffer();
					}
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
	}

	ShaderObjectPtr D3D11ShaderObject::Clone(RenderEffect const & effect)
	{
		D3D11ShaderObjectPtr ret = MakeSharedPtr<D3D11ShaderObject>(so_template_);

		ret->has_discard_ = has_discard_;
		ret->is_validate_ = is_validate_;
		ret->uavsrcs_.resize(uavsrcs_.size(), nullptr);
		ret->uavs_.resize(uavs_.size());

		std::vector<uint32_t> all_cbuff_indices;
		for (size_t i = 0; i < NumShaderStages; ++ i)
		{
			ret->samplers_[i] = samplers_[i];
			ret->srvsrcs_[i].resize(srvsrcs_[i].size(), std::make_tuple(static_cast<void*>(nullptr), 0, 0));
			ret->srvs_[i].resize(srvs_[i].size());

			auto const* shader_stage = checked_cast<D3D11ShaderStageObject*>(this->Stage(static_cast<ShaderStage>(i)).get());
			if (shader_stage && !shader_stage->CBufferIndices().empty())
			{
				auto const& cbuff_indices = shader_stage->CBufferIndices();

				ret->d3d11_cbuffs_[i].resize(d3d11_cbuffs_[i].size());
				all_cbuff_indices.insert(all_cbuff_indices.end(), cbuff_indices.begin(), cbuff_indices.end());
				for (size_t j = 0; j < cbuff_indices.size(); ++ j)
				{
					auto cbuff = effect.CBufferByIndex(cbuff_indices[j]);
					ret->d3d11_cbuffs_[i][j] = checked_cast<D3D11GraphicsBuffer*>(cbuff->HWBuff().get())->D3DBuffer();
				}
			}

			ret->param_binds_[i].reserve(param_binds_[i].size());
			for (auto const & pb : param_binds_[i])
			{
				ret->param_binds_[i].push_back(ret->GetBindFunc(static_cast<ShaderStage>(i), pb.offset,
					effect.ParameterByName(pb.param->Name())));
			}
		}

		std::sort(all_cbuff_indices.begin(), all_cbuff_indices.end());
		all_cbuff_indices.erase(std::unique(all_cbuff_indices.begin(), all_cbuff_indices.end()), all_cbuff_indices.end());
		ret->all_cbuffs_.resize(all_cbuff_indices.size());
		for (size_t i = 0; i < all_cbuff_indices.size(); ++ i)
		{
			ret->all_cbuffs_[i] = effect.CBufferByIndex(all_cbuff_indices[i]);
		}

		return ret;
	}

	D3D11ShaderObject::ParameterBind D3D11ShaderObject::GetBindFunc(ShaderStage stage, uint32_t offset, RenderEffectParameter* param)
	{
		uint32_t const stage_index = static_cast<uint32_t>(stage);

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
			ret.func = SetD3D11ShaderParameterTextureSRV(srvsrcs_[stage_index][offset], srvs_[stage_index][offset], param);
			break;

		case REDT_buffer:
		case REDT_structured_buffer:
		case REDT_consume_structured_buffer:
		case REDT_append_structured_buffer:
		case REDT_byte_address_buffer:
			ret.func = SetD3D11ShaderParameterGraphicsBufferSRV(srvsrcs_[stage_index][offset], srvs_[stage_index][offset], param);
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
			ret.func = SetD3D11ShaderParameterTextureUAV(uavsrcs_[offset], uavs_[offset], param);
			break;

		case REDT_rw_buffer:
		case REDT_rw_structured_buffer:
		case REDT_rw_byte_address_buffer:
		case REDT_rasterizer_ordered_buffer:
		case REDT_rasterizer_ordered_structured_buffer:
		case REDT_rasterizer_ordered_byte_address_buffer:
			ret.func = SetD3D11ShaderParameterGraphicsBufferUAV(uavsrcs_[offset], uavs_[offset], param);
			break;

		default:
			KFL_UNREACHABLE("Invalid type");
		}

		return ret;
	}

	void D3D11ShaderObject::Bind()
	{
		D3D11RenderEngine& re = *checked_cast<D3D11RenderEngine*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());

		if (this->Stage(ShaderStage::ComputeShader))
		{
			re.CSSetShader(checked_cast<D3D11ShaderStageObject*>(this->Stage(ShaderStage::ComputeShader).get())
							   ->HwComputeShader());
		}
		else
		{
			auto const* vs_stage = checked_cast<D3D11ShaderStageObject*>(this->Stage(ShaderStage::VertexShader).get());
			re.VSSetShader(vs_stage ? vs_stage->HwVertexShader() : nullptr);

			auto const* ps_stage = checked_cast<D3D11ShaderStageObject*>(this->Stage(ShaderStage::PixelShader).get());
			re.PSSetShader(ps_stage ? ps_stage->HwPixelShader() : nullptr);

			ID3D11HullShader* hull_shader = nullptr;
			ID3D11DomainShader* domain_shader = nullptr;
			if (this->Stage(ShaderStage::HullShader))
			{
				hull_shader =
					checked_cast<D3D11ShaderStageObject*>(this->Stage(ShaderStage::HullShader).get())->HwHullShader();
				domain_shader = checked_cast<D3D11ShaderStageObject*>(this->Stage(ShaderStage::DomainShader).get())
									->HwDomainShader();
			}
			re.HSSetShader(hull_shader);
			re.DSSetShader(domain_shader);

			ShaderStage geometry_stage = ShaderStage::Num;
			if (this->Stage(ShaderStage::GeometryShader))
			{
				geometry_stage = ShaderStage::GeometryShader;
			}
			else if (this->Stage(ShaderStage::DomainShader))
			{
				geometry_stage = ShaderStage::DomainShader;
			}
			else if (this->Stage(ShaderStage::VertexShader))
			{
				geometry_stage = ShaderStage::VertexShader;
			}
			re.GSSetShader((geometry_stage != ShaderStage::Num)
							   ? checked_cast<D3D11ShaderStageObject*>(this->Stage(geometry_stage).get())->HwGeometryShader()
							   : nullptr);
		}

		for (auto const & pbs : param_binds_)
		{
			for (auto const & pb : pbs)
			{
				pb.func();
			}
		}

		for (auto cb : all_cbuffs_)
		{
			cb->Update();
		}

		for (size_t stage_index = 0; stage_index < NumShaderStages; ++stage_index)
		{
			ShaderStage const stage = static_cast<ShaderStage>(stage_index);
			if (!srvs_[stage_index].empty())
			{
				re.SetShaderResources(stage, srvsrcs_[stage_index], srvs_[stage_index]);
			}

			if (!samplers_[stage_index].empty())
			{
				re.SetSamplers(stage, samplers_[stage_index]);
			}

			if (!d3d11_cbuffs_[stage_index].empty())
			{
				re.SetConstantBuffers(stage, d3d11_cbuffs_[stage_index]);
			}
		}

		if (this->Stage(ShaderStage::ComputeShader) && !uavs_.empty())
		{
			for (uint32_t i = 0; i < uavs_.size(); ++ i)
			{
				if (uavsrcs_[i] != nullptr)
				{
					re.DetachSRV(uavsrcs_[i], 0, 1);
				}
			}

			std::vector<UINT> uav_init_counts(uavs_.size(), 0);
			re.CSSetUnorderedAccessViews(0, static_cast<UINT>(uavs_.size()), &uavs_[0], &uav_init_counts[0]);
		}
	}

	void D3D11ShaderObject::Unbind()
	{
		if (this->Stage(ShaderStage::ComputeShader) && !uavs_.empty())
		{
			D3D11RenderEngine& re = *checked_cast<D3D11RenderEngine*>(&Context::Instance().RenderFactoryInstance().RenderEngineInstance());

			std::vector<ID3D11UnorderedAccessView*> uavs(uavs_.size(), nullptr);
			std::vector<UINT> uav_init_counts(uavs_.size(), 0);
			re.CSSetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), &uavs[0], &uav_init_counts[0]);
		}
	}

	ArrayRef<uint8_t> D3D11ShaderObject::VsCode() const
	{
		return MakeArrayRef(checked_cast<D3D11ShaderStageObject*>(this->Stage(ShaderStage::VertexShader).get())->ShaderCodeBlob());
	}

	uint32_t D3D11ShaderObject::VsSignature() const
	{
		return checked_cast<D3D11VertexShaderStageObject*>(this->Stage(ShaderStage::VertexShader).get())->VsSignature();
	}
}
