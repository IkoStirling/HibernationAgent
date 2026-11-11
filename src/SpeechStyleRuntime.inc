    Json expressionPreference()const{
        if(cfg.channel)return cfg.channel->expressionPreference.empty()?speechStyle::preference(speechStyle::defaults()):speechStyle::preference(Json{{"preference",cfg.channel->expressionPreference}});
        if(plugins&&plugins->enabled("speech-style"))return speechStyle::preference(plugins->liveSnapshots().value("speech-style",Json::object()));
        return speechStyle::preference(speechStyle::defaults());
    }
