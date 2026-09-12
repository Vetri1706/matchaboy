#include "friend_session.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include "dmg/socket_platform.hpp"
#include "link_fixture.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <stdexcept>
static void check(bool value,const std::string &why){if(!value)throw std::runtime_error(why);}
int main(){try{
    namespace sp=dmg::netplay::socket_platform;
    sp::Runtime sockets;std::uint16_t port{};
    {sp::Socket socket(sp::create_datagram());
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    check(bind(socket.get(),reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"bind");
    sp::AddressLength length=sizeof(address);check(getsockname(socket.get(),reinterpret_cast<sockaddr*>(&address),&length)==0,"port");
    port=ntohs(address.sin_port);}
    const auto folder=std::filesystem::temp_directory_path()/("matchaboy-friend-test-"+std::to_string(port));
    std::filesystem::create_directories(folder);const auto path=folder/"cable.gb";
    const auto rom=linked_gb_rom();{std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(rom.data()),rom.size());}
    dmg::Bus a(rom),b(rom);dmg::Cpu ac(a),bc(b);a.poke(0xC001,1);b.poke(0xC001,0);
    const auto code=matcha::FriendSession::make_room_code();check(code.size()==32,"room code length");
    matcha::FriendSession host({true,"127.0.0.1",port,code},path,&a,&ac,nullptr);
    matcha::FriendSession join({false,"127.0.0.1",port,code},path,&b,&bc,nullptr);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(40);
    while((host.frames()<1000||join.frames()<1000)&&std::chrono::steady_clock::now()<deadline){
        if(host.frames()<1000)host.advance(0x10);
        if(join.frames()<1000)join.advance(0x20);
        check(!host.finished(),host.status());check(!join.finished(),join.status());
        if(host.frames()==join.frames()&&host.frames()==0)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(host.frames()==1000&&join.frames()==1000,"1000 linked network frames must finish");
    check(a.peek(0xC000)==0xDD && b.peek(0xC000)==0xDE,"network inputs must drive actual linked serial bytes");
    const auto cycles=a.cycles();
    const auto diagnostics=host.diagnostics();
    check(diagnostics.find(code)==std::string::npos && diagnostics.find("127.0.0.1")==std::string::npos &&
          diagnostics.find(path.string())==std::string::npos,"shareable diagnostics must exclude code, address and ROM path");
    check(host.frames()==1000 && a.cycles()==cycles,"reading diagnostics must not advance the linked machines");
    host.close();
    check(host.diagnostics().find("Frame: 1000")!=std::string::npos,"closed diagnostics retain the failure timeline");
    for(unsigned i=0;i<100&&!join.finished();++i){join.advance(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(join.finished(),"peer disconnect must stop session");
    join.close();std::filesystem::remove_all(folder);
    std::cout<<"PASS1000 real UDP friend frames, distinct saves/state, serial transfer, desync checks and disconnect\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
