#include <KlayGE/KlayGE.hpp>
#include <KlayGE/ThrowErr.hpp>
#include <KlayGE/VertexBuffer.hpp>
#include <KlayGE/Math.hpp>
#include <KlayGE/Font.hpp>
#include <KlayGE/Renderable.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderEffect.hpp>
#include <KlayGE/SceneManager.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/ResLoader.hpp>
#include <KlayGE/Texture.hpp>

#include <KlayGE/D3D9/D3D9RenderSettings.hpp>
#include <KlayGE/D3D9/D3D9RenderFactory.hpp>

#include <vector>
#include <sstream>
#include <ctime>

#include "Fractal.hpp"

using namespace std;
using namespace KlayGE;

namespace
{
	class RenderFractal : public Renderable
	{
	public:
		RenderFractal()
			: vb_(new VertexBuffer(VertexBuffer::BT_TriangleList))
		{
			effect_ = LoadRenderEffect("Fractal.fx");

			if (!effect_->SetTechnique("FractalPS30"))
			{
				if (!effect_->SetTechnique("FractalPS2a"))
				{
					effect_->SetTechnique("FractalPS20");
				}
			}

			Vector3 xyzs[] =
			{
				Vector3(-0.8f, 0.8f, 1),
				Vector3(0.8f, 0.8f, 1),
				Vector3(0.8f, -0.8f, 1),
				Vector3(-0.8f, -0.8f, 1),
			};

			Vector2 texs[] =
			{
				Vector2(-1.85f, 1.2f),
				Vector2(0.5f, 1.2f),
				Vector2(0.5f, -1.2f),
				Vector2(-1.85f, -1.2f),
			};

			uint16_t indices[] = 
			{
				0, 1, 2, 2, 3, 0,
			};

			box_ = MathLib::ComputeBoundingBox<float>(&xyzs[0], &xyzs[0] + sizeof(xyzs) / sizeof(xyzs[0]));

			vb_->AddVertexStream(VST_Positions, sizeof(float), 3);
			vb_->AddVertexStream(VST_TextureCoords0, sizeof(float), 2);
			vb_->GetVertexStream(VST_Positions)->Assign(xyzs, sizeof(xyzs) / sizeof(xyzs[0]));
			vb_->GetVertexStream(VST_TextureCoords0)->Assign(texs, sizeof(texs) / sizeof(texs[0]));

			vb_->AddIndexStream();
			vb_->GetIndexStream()->Assign(indices, sizeof(indices) / sizeof(uint16_t));
		}

		RenderEffectPtr GetRenderEffect() const
		{
			return effect_;
		}

		VertexBufferPtr GetVertexBuffer() const
		{
			return vb_;
		}

		Box GetBound() const
		{
			return box_;
		}

		std::wstring const & Name() const
		{
			static const std::wstring name(L"Fractal");
			return name;
		}

	private:
		KlayGE::VertexBufferPtr vb_;
		KlayGE::RenderEffectPtr effect_;

		Box box_;
	};

	class TheRenderSettings : public D3D9RenderSettings
	{
	private:
		bool DoConfirmDevice(D3DCAPS9 const & caps, uint32_t behavior, D3DFORMAT format) const
		{
			if (caps.VertexShaderVersion < D3DVS_VERSION(1, 1))
			{
				return false;
			}
			if (caps.PixelShaderVersion < D3DPS_VERSION(2, 0))
			{
				return false;
			}
			return true;
		}
	};
}

int main()
{
	SceneManager sceneMgr;
	Context::Instance().RenderFactoryInstance(D3D9RenderFactoryInstance());
	Context::Instance().SceneManagerInstance(sceneMgr);

	TheRenderSettings settings;
	settings.width = 800;
	settings.height = 600;
	settings.colorDepth = 32;
	settings.fullScreen = false;

	Fractal app;
	app.Create("Fractal", settings);
	app.Run();

	return 0;
}

Fractal::Fractal()
{
	ResLoader::Instance().AddPath("../media");
	ResLoader::Instance().AddPath("../media/Fractal");
}

void Fractal::InitObjects()
{
	font_ = Context::Instance().RenderFactoryInstance().MakeFont("gbsn00lp.ttf", 16);

	renderFractal_.reset(new RenderFractal);
	renderFractal_->AddToSceneManager();

	RenderEngine& renderEngine(Context::Instance().RenderFactoryInstance().RenderEngineInstance());
	renderEngine.ClearColor(Color(0.2f, 0.4f, 0.6f, 1));
}

void Fractal::Update()
{
	RenderEngine& renderEngine(Context::Instance().RenderFactoryInstance().RenderEngineInstance());

	std::wostringstream stream;
	stream << (*renderEngine.ActiveRenderTarget())->FPS();

	font_->RenderText(0, 0, Color(1, 1, 0, 1), L"GPU�������");
	font_->RenderText(0, 18, Color(1, 1, 0, 1), stream.str());
}
