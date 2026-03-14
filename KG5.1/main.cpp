#include "Window.h"
#include "Game.h"
#include <memory>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    auto window = std::make_unique<Window>(hInstance, 1280, 720, L"DX12 Deferred Rendering");
    if (!window->Initialize())
    {
        MessageBox(nullptr, L"Window initialization failed!", L"Error", MB_OK);
        return -1;
    }

    auto game = std::make_unique<Game>(
        window->GetHWND(), window->GetWidth(), window->GetHeight());

    window->OnResize = [&](int w, int h)
    {
        if (game) game->Resize(w, h);
    };

    if (!game->Initialize())
        return -1;

    return window->Run(
        [&](float dt)
        {
            window->GetInputDevice()->Update();
            // Передаём InputDevice в Update — нужен для управления камерой
            game->Update(dt, window->GetInputDevice());
        },
        [&]()
        {
            game->Render();
        }
    );
}
