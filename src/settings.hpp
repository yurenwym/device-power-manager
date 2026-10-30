#pragma once
#include "adapters.hpp"
namespace battery {
struct Settings { unsigned interval=60, threshold=20; bool notify=true; Bindings bindings; };
std::wstring data_folder();
std::wstring executable();
Settings load_settings();
bool save_settings(const Settings&);
bool startup_enabled();
HRESULT set_startup(bool enabled);
}
