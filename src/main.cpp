#include "PCH.h"

#include "CrosshairWatcher.h"
#include "HighlightManager.h"
#include "OutlineRenderer.h"
#include "Settings.h"

namespace
{
	void InitLogging()
	{
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= "ItemOutline.log";

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
	}

	// Fired by SKSE at well-defined lifecycle points.
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Settings::GetSingleton()->Load();
			CrosshairWatcher::GetSingleton()->Install();
			logger::info("ItemOutline ready - watching crosshair.");
			break;

		// The swap chain only exists once the renderer is up, so the Present hook
		// goes here rather than at kDataLoaded. Both messages are handled because
		// either can be the first time we reach a live render loop.
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			OutlineRenderer::GetSingleton()->InstallHook();
			if (Settings::GetSingleton()->preUIHook) {
				OutlineRenderer::GetSingleton()->InstallPreUIHook();
			}
			break;

		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitLogging();

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("Loading {} v{}", plugin->GetName(), plugin->GetVersion().string());

	SKSE::Init(a_skse);

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		logger::critical("Failed to register SKSE message listener.");
		return false;
	}

	return true;
}
