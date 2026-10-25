    void processPluginEvents(std::int64_t now){for(auto event:plugins->takeEvents()){
            if(event.value("kind","")=="ui.chat.open"){
                if(cfg.desktopEnabled&&event.value("plugin","")=="widget"&&event.contains("x")&&event.contains("y")&&event.at("x").is_number_integer()&&event.at("y").is_number_integer()&&std::abs(event.at("x").get<double>())<=100000&&std::abs(event.at("y").get<double>())<=100000){desktopRequests=Json::array({{{"x",event.at("x")},{"y",event.at("y")},{"mode",event.value("mode",std::string("floating"))}}});}
                continue;
            }
            if(event.value("kind","")=="speech.prepared"){reactionPrepared(event);continue;}
            trackSpeech(event);
            if(event.value("kind","")=="speech.failed")speechIssue="语音未能播放，请在管理页检查声音设置。";
            if(event.value("kind","")=="speech.started")speechIssue.clear();
            if(event.value("kind","")=="weather.observed")life.weather(event.at("temperature"),event.at("weather_code"),event.value("observed_ms",now));
            plugins->event(event);
            if(event.value("kind","")=="speech.progress"||event.value("kind","")=="widget.position")continue; // Playback and position telemetry are ephemeral.
            const auto source=recordPluginEvent(event,now);if(event.value("kind","")=="widget.interacted")reactToDrag(event,source,now);
        }}
