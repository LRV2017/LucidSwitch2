#include "lucid/devices/sdl_host_io.hpp"
#include <algorithm>
#include <cstdint>
#ifndef LUCID_HAS_SDL2
#define LUCID_HAS_SDL2 0
#endif
#if LUCID_HAS_SDL2
#include <SDL.h>
#include <SDL_gamecontroller.h>
#endif
namespace lucid::devices {
namespace {
void set_error(std::string* error,const char* message) {
    if(error!=nullptr)*error=message;
}
#if LUCID_HAS_SDL2
void SDLCALL audio_callback(void* userdata,Uint8* stream,const int length) {
    if(stream==nullptr||length<=0)return;
    SDL_memset(stream,0,static_cast<std::size_t>(length));
    auto* mixer=static_cast<AudioMixer*>(userdata);
    if(mixer==nullptr)return;
    const std::size_t sample_count=static_cast<std::size_t>(length)/sizeof(std::int16_t);
    if((sample_count%2U)!=0U)return;
    auto* samples=reinterpret_cast<std::int16_t*>(stream);
    (void)mixer->mix_stereo(std::span<std::int16_t>(samples,sample_count));
}
std::uint32_t read_buttons(SDL_GameController* controller) noexcept {
    std::uint32_t buttons=0U;
    const auto add=[&](const SDL_GameControllerButton source,
                       const ControllerButton destination) {
        if(SDL_GameControllerGetButton(controller,source)!=0U)
            buttons|=button_bit(destination);
    };
    add(SDL_CONTROLLER_BUTTON_A,ControllerButton::A);
    add(SDL_CONTROLLER_BUTTON_B,ControllerButton::B);
    add(SDL_CONTROLLER_BUTTON_X,ControllerButton::X);
    add(SDL_CONTROLLER_BUTTON_Y,ControllerButton::Y);
    add(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,ControllerButton::L);
    add(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,ControllerButton::R);
    add(SDL_CONTROLLER_BUTTON_BACK,ControllerButton::Minus);
    add(SDL_CONTROLLER_BUTTON_START,ControllerButton::Plus);
    add(SDL_CONTROLLER_BUTTON_LEFTSTICK,ControllerButton::LStick);
    add(SDL_CONTROLLER_BUTTON_RIGHTSTICK,ControllerButton::RStick);
    add(SDL_CONTROLLER_BUTTON_DPAD_UP,ControllerButton::Up);
    add(SDL_CONTROLLER_BUTTON_DPAD_DOWN,ControllerButton::Down);
    add(SDL_CONTROLLER_BUTTON_DPAD_LEFT,ControllerButton::Left);
    add(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,ControllerButton::Right);
    return buttons;
}
std::uint16_t read_trigger(SDL_GameController* controller,
                           const SDL_GameControllerAxis axis) noexcept {
    const Sint16 raw=SDL_GameControllerGetAxis(controller,axis);
    const auto positive=std::max<Sint32>(0,static_cast<Sint32>(raw));
    return static_cast<std::uint16_t>(std::min<Sint32>(positive,32767));
}
#endif
}
SdlHostIo::SdlHostIo(ControllerHub& controllers,AudioMixer& mixer) noexcept
    : controllers_(controllers),mixer_(mixer) {
    instance_ids_.fill(-1);
}
SdlHostIo::~SdlHostIo(){shutdown();}
bool SdlHostIo::compiled_with_sdl2() noexcept {
#if LUCID_HAS_SDL2
    return true;
#else
    return false;
#endif
}
bool SdlHostIo::initialize(std::string* error) {
#if LUCID_HAS_SDL2
    if(initialized_)return true;
    if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER|SDL_INIT_AUDIO)!=0){
        set_error(error,SDL_GetError());return false;
    }
    initialized_=true;
    SDL_GameControllerEventState(SDL_ENABLE);
    return true;
#else
    set_error(error,"LucidSwitch2 was built without SDL2 host-device support.");
    return false;
#endif
}
void SdlHostIo::shutdown() noexcept {
#if LUCID_HAS_SDL2
    close_audio_device();
    for(std::size_t slot=0U;slot<controller_handles_.size();++slot){
        auto* controller=static_cast<SDL_GameController*>(controller_handles_[slot]);
        if(controller!=nullptr)SDL_GameControllerClose(controller);
        controller_handles_[slot]=nullptr;
        instance_ids_[slot]=-1;
        (void)controllers_.disconnect(slot);
    }
    if(initialized_){
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER|SDL_INIT_AUDIO);
        initialized_=false;
    }
#endif
}
std::size_t SdlHostIo::poll_controllers() noexcept {
#if LUCID_HAS_SDL2
    if(!initialized_)return 0U;
    SDL_GameControllerUpdate();
    for(std::size_t slot=0U;slot<controller_handles_.size();++slot){
        auto* controller=static_cast<SDL_GameController*>(controller_handles_[slot]);
        if(controller!=nullptr&&!SDL_GameControllerGetAttached(controller)){
            SDL_GameControllerClose(controller);
            controller_handles_[slot]=nullptr;
            instance_ids_[slot]=-1;
            (void)controllers_.disconnect(slot);
        }
    }
    const int device_count=SDL_NumJoysticks();
    if(device_count>0){
        for(int device=0;device<device_count;++device){
            if(SDL_IsGameController(device)==SDL_FALSE)continue;
            const SDL_JoystickID instance=SDL_JoystickGetDeviceInstanceID(device);
            if(instance<0)continue;
            bool already_open=false;
            for(const auto id:instance_ids_)if(id==instance){already_open=true;break;}
            if(already_open)continue;
            std::size_t free_slot=controller_handles_.size();
            for(std::size_t slot=0U;slot<controller_handles_.size();++slot)
                if(controller_handles_[slot]==nullptr){free_slot=slot;break;}
            if(free_slot==controller_handles_.size())break;
            auto* controller=SDL_GameControllerOpen(device);
            if(controller==nullptr)continue;
            controller_handles_[free_slot]=controller;
            instance_ids_[free_slot]=instance;
            (void)controllers_.connect(free_slot);
        }
    }
    std::size_t connected=0U;
    for(std::size_t slot=0U;slot<controller_handles_.size();++slot){
        auto* controller=static_cast<SDL_GameController*>(controller_handles_[slot]);
        if(controller==nullptr)continue;
        ControllerSnapshot state{};
        state.buttons=read_buttons(controller);
        state.left_x=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTX);
        state.left_y=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTY);
        state.right_x=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_RIGHTX);
        state.right_y=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_RIGHTY);
        state.left_trigger=read_trigger(controller,SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        state.right_trigger=read_trigger(controller,SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if(controllers_.publish(slot,state))++connected;
    }
    return connected;
#else
    return 0U;
#endif
}
bool SdlHostIo::open_audio_device(std::string* error) {
#if LUCID_HAS_SDL2
    if(!initialized_){
        set_error(error,"Initialize SDL2 host I/O before opening the audio device.");
        return false;
    }
    if(audio_device_id_!=0U)return true;
    SDL_AudioSpec desired{};
    desired.freq=static_cast<int>(AudioMixer::SampleRate);
    desired.format=AUDIO_S16SYS;
    desired.channels=2U;
    desired.samples=1024U;
    desired.callback=audio_callback;
    desired.userdata=&mixer_;
    SDL_AudioSpec obtained{};
    const SDL_AudioDeviceID device=SDL_OpenAudioDevice(nullptr,0,&desired,&obtained,0);
    if(device==0U){set_error(error,SDL_GetError());return false;}
    if(obtained.freq!=desired.freq||obtained.format!=desired.format||
       obtained.channels!=desired.channels){
        SDL_CloseAudioDevice(device);
        set_error(error,"The selected audio device cannot provide 48 kHz signed-16-bit stereo PCM.");
        return false;
    }
    audio_device_id_=device;
    SDL_PauseAudioDevice(device,0);
    return true;
#else
    set_error(error,"LucidSwitch2 was built without SDL2 host-device support.");
    return false;
#endif
}
void SdlHostIo::close_audio_device() noexcept {
#if LUCID_HAS_SDL2
    if(audio_device_id_!=0U){
        SDL_PauseAudioDevice(audio_device_id_,1);
        SDL_CloseAudioDevice(audio_device_id_);
        audio_device_id_=0U;
    }
#endif
}
bool SdlHostIo::audio_device_open() const noexcept{return audio_device_id_!=0U;}
} // namespace lucid::devices
