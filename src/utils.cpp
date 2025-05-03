#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <random>
#include <string>
#include <optional>
#include <vector>
#include <regex>
#include <algorithm>
#include <fstream>
#include "../include/utils.h"

namespace tg_forwarder {

bool is_media_message(const Message& message) {
    if (!message) {
        return false;
    }
    
    const auto content = message->content_.get();
    return content->get_id() == td_api::messagePhoto::ID ||
           content->get_id() == td_api::messageVideo::ID ||
           content->get_id() == td_api::messageDocument::ID ||
           content->get_id() == td_api::messageAudio::ID ||
           content->get_id() == td_api::messageAnimation::ID ||
           content->get_id() == td_api::messageSticker::ID ||
           content->get_id() == td_api::messageVoiceNote::ID ||
           content->get_id() == td_api::messageVideoNote::ID;
}

std::vector<Int32> get_file_ids(const Message& message) {
    std::vector<Int32> file_ids;
    
    if (!message) {
        return file_ids;
    }
    
    const auto content = message->content_.get();
    
    switch (content->get_id()) {
        case td_api::messagePhoto::ID: {
            auto photo = static_cast<const td_api::messagePhoto*>(content);
            for (const auto& size : photo->photo_->sizes_) {
                file_ids.push_back(size->photo_->id_);
            }
            break;
        }
        case td_api::messageVideo::ID: {
            auto video = static_cast<const td_api::messageVideo*>(content);
            file_ids.push_back(video->video_->video_->id_);
            if (video->video_->thumbnail_) {
                file_ids.push_back(video->video_->thumbnail_->file_->id_);
            }
            break;
        }
        case td_api::messageDocument::ID: {
            auto document = static_cast<const td_api::messageDocument*>(content);
            file_ids.push_back(document->document_->document_->id_);
            if (document->document_->thumbnail_) {
                file_ids.push_back(document->document_->thumbnail_->file_->id_);
            }
            break;
        }
        case td_api::messageAudio::ID: {
            auto audio = static_cast<const td_api::messageAudio*>(content);
            file_ids.push_back(audio->audio_->audio_->id_);
            if (audio->audio_->album_cover_thumbnail_) {
                file_ids.push_back(audio->audio_->album_cover_thumbnail_->file_->id_);
            }
            break;
        }
        case td_api::messageAnimation::ID: {
            auto animation = static_cast<const td_api::messageAnimation*>(content);
            file_ids.push_back(animation->animation_->animation_->id_);
            if (animation->animation_->thumbnail_) {
                file_ids.push_back(animation->animation_->thumbnail_->file_->id_);
            }
            break;
        }
        case td_api::messageSticker::ID: {
            auto sticker = static_cast<const td_api::messageSticker*>(content);
            file_ids.push_back(sticker->sticker_->sticker_->id_);
            if (sticker->sticker_->thumbnail_) {
                file_ids.push_back(sticker->sticker_->thumbnail_->file_->id_);
            }
            break;
        }
        case td_api::messageVoiceNote::ID: {
            auto voice = static_cast<const td_api::messageVoiceNote*>(content);
            file_ids.push_back(voice->voice_note_->voice_->id_);
            break;
        }
        case td_api::messageVideoNote::ID: {
            auto video_note = static_cast<const td_api::messageVideoNote*>(content);
            file_ids.push_back(video_note->video_note_->video_->id_);
            if (video_note->video_note_->thumbnail_) {
                file_ids.push_back(video_note->video_note_->thumbnail_->file_->id_);
            }
            break;
        }
    }
    
    return file_ids;
}

std::optional<std::string> get_media_group_id(const Message& message) {
    if (!message) {
        return std::nullopt;
    }
    
    // 检查消息类型
    int32_t content_type = message->content_->get_id();
    
    // 检查具有媒体组的消息类型
    if (content_type == td_api::messagePhoto::ID) {
        auto photo = static_cast<const td_api::messagePhoto*>(message->content_.get());
        if (!photo->media_album_id_.empty()) {
            return photo->media_album_id_;
        }
    } else if (content_type == td_api::messageVideo::ID) {
        auto video = static_cast<const td_api::messageVideo*>(message->content_.get());
        if (!video->media_album_id_.empty()) {
            return video->media_album_id_;
        }
    } else if (content_type == td_api::messageDocument::ID) {
        auto document = static_cast<const td_api::messageDocument*>(message->content_.get());
        if (!document->media_album_id_.empty()) {
            return document->media_album_id_;
        }
    } else if (content_type == td_api::messageAudio::ID) {
        auto audio = static_cast<const td_api::messageAudio*>(message->content_.get());
        if (!audio->media_album_id_.empty()) {
            return audio->media_album_id_;
        }
    }
    
    return std::nullopt;
}

std::string get_caption(const Message& message) {
    if (!message) {
        return "";
    }
    
    const auto content = message->content_.get();
    
    switch (content->get_id()) {
        case td_api::messageText::ID: {
            auto text = static_cast<const td_api::messageText*>(content);
            return text->text_->text_;
        }
        case td_api::messagePhoto::ID: {
            auto photo = static_cast<const td_api::messagePhoto*>(content);
            return photo->caption_->text_;
        }
        case td_api::messageVideo::ID: {
            auto video = static_cast<const td_api::messageVideo*>(content);
            return video->caption_->text_;
        }
        case td_api::messageDocument::ID: {
            auto document = static_cast<const td_api::messageDocument*>(content);
            return document->caption_->text_;
        }
        case td_api::messageAudio::ID: {
            auto audio = static_cast<const td_api::messageAudio*>(content);
            return audio->caption_->text_;
        }
        case td_api::messageAnimation::ID: {
            auto animation = static_cast<const td_api::messageAnimation*>(content);
            return animation->caption_->text_;
        }
        case td_api::messageVoiceNote::ID: {
            auto voice = static_cast<const td_api::messageVoiceNote*>(content);
            return voice->caption_->text_;
        }
        default:
            return "";
    }
}

std::string get_text(const Message& message) {
    if (!message) {
        return "";
    }
    
    const auto content = message->content_.get();
    
    if (content->get_id() == td_api::messageText::ID) {
        auto text = static_cast<const td_api::messageText*>(content);
        return text->text_->text_;
    }
    
    return get_caption(message);
}

void delay(int seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

std::string generate_message_id(Int64 chat_id, Int64 message_id) {
    return std::to_string(chat_id) + "_" + std::to_string(message_id);
}

void MemoryBuffer::append(const std::string& data) {
    binary_data_ += data;
}

const std::string& MemoryBuffer::data() const {
    return binary_data_;
}

void MemoryBuffer::clear() {
    binary_data_.clear();
}

size_t MemoryBuffer::size() const {
    return binary_data_.size();
}

void MemoryBuffer::set_name(const std::string& name) {
    name_ = name;
}

const std::string& MemoryBuffer::name() const {
    return name_;
}

MediaType get_media_type(const Message& message) {
    if (!message) {
        return MediaType::Unknown;
    }
    
    const auto content = message->content_.get();
    
    switch (content->get_id()) {
        case td_api::messagePhoto::ID:
            return MediaType::Photo;
        case td_api::messageVideo::ID:
            return MediaType::Video;
        case td_api::messageDocument::ID:
            return MediaType::Document;
        case td_api::messageAudio::ID:
            return MediaType::Audio;
        case td_api::messageAnimation::ID:
            return MediaType::Animation;
        case td_api::messageSticker::ID:
            return MediaType::Sticker;
        case td_api::messageVoiceNote::ID:
            return MediaType::VoiceNote;
        case td_api::messageVideoNote::ID:
            return MediaType::VideoNote;
        default:
            return MediaType::Unknown;
    }
}

std::string media_type_to_string(MediaType type) {
    switch (type) {
        case MediaType::Photo:
            return "photo";
        case MediaType::Video:
            return "video";
        case MediaType::Document:
            return "document";
        case MediaType::Audio:
            return "audio";
        case MediaType::Animation:
            return "animation";
        case MediaType::Sticker:
            return "sticker";
        case MediaType::VoiceNote:
            return "voice_note";
        case MediaType::VideoNote:
            return "video_note";
        default:
            return "unknown";
    }
}

std::string get_file_extension(MediaType type, const Message& message) {
    switch (type) {
        case MediaType::Photo:
            return ".jpg";
        case MediaType::Video: {
            if (message && message->content_->get_id() == td_api::messageVideo::ID) {
                auto video = static_cast<const td_api::messageVideo*>(message->content_.get());
                auto mime_type = video->video_.mime_type_;
                
                if (mime_type == "video/mp4") {
                    return ".mp4";
                } else if (mime_type == "video/webm") {
                    return ".webm";
                } else if (mime_type == "video/x-matroska") {
                    return ".mkv";
                }
            }
            
            return ".mp4"; // 默认扩展名
        }
        case MediaType::Document: {
            if (message && message->content_->get_id() == td_api::messageDocument::ID) {
                auto document = static_cast<const td_api::messageDocument*>(message->content_.get());
                auto file_name = document->document_.file_name_;
                
                // 尝试从文件名中提取扩展名
                size_t pos = file_name.find_last_of('.');
                if (pos != std::string::npos) {
                    return file_name.substr(pos);
                }
                
                // 根据MIME类型猜测扩展名
                auto mime_type = document->document_.mime_type_;
                
                if (mime_type == "application/pdf") {
                    return ".pdf";
                } else if (mime_type == "application/zip") {
                    return ".zip";
                } else if (mime_type == "application/x-rar-compressed") {
                    return ".rar";
                } else if (mime_type == "text/plain") {
                    return ".txt";
                } else if (mime_type == "application/msword") {
                    return ".doc";
                } else if (mime_type == "application/vnd.openxmlformats-officedocument.wordprocessingml.document") {
                    return ".docx";
                }
            }
            
            return ".bin"; // 默认扩展名
        }
        case MediaType::Audio: {
            if (message && message->content_->get_id() == td_api::messageAudio::ID) {
                auto audio = static_cast<const td_api::messageAudio*>(message->content_.get());
                auto mime_type = audio->audio_.mime_type_;
                
                if (mime_type == "audio/mpeg") {
                    return ".mp3";
                } else if (mime_type == "audio/ogg") {
                    return ".ogg";
                } else if (mime_type == "audio/x-wav") {
                    return ".wav";
                } else if (mime_type == "audio/x-flac") {
                    return ".flac";
                }
            }
            
            return ".mp3"; // 默认扩展名
        }
        case MediaType::Animation:
            return ".mp4"; // 动画通常以MP4格式存储
        case MediaType::Sticker: {
            if (message && message->content_->get_id() == td_api::messageSticker::ID) {
                auto sticker = static_cast<const td_api::messageSticker*>(message->content_.get());
                
                if (sticker->sticker_.is_animated_) {
                    return ".tgs"; // 动画贴纸
                } else {
                    return ".webp"; // 静态贴纸
                }
            }
            
            return ".webp"; // 默认扩展名
        }
        default:
            return ".bin"; // 默认二进制扩展名
    }
}

} // namespace tg_forwarder 