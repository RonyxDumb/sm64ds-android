#include "rom_probe.h"
#include "ntr/fs.h"
#include "ntr/lz77.h"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>
#include <fstream>
#include <iterator>
#include <string>
#include <stdexcept>
void verify_game_math();
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    try {
        verify_game_math();
        const uint8_t literal[] = {0x10, 3, 0, 0, 0, 'a', 'b', 'c'};
        uint8_t out[3]{};
        check(ntr::lz77_unpack(literal, sizeof literal, out, 3) == 3 && out[2] == 'c', "literal decode");
        const uint8_t bad[] = {0x10, 3, 0, 0, 0x80, 0, 0};
        check(ntr::lz77_unpack(bad, sizeof bad, out, 3) == -1, "invalid backreference");
        check(ntr::lz77_unpack(literal, 7, out, 3) == -1, "truncated LZ stream");
        check(ntr::lz77_unpack(literal, sizeof literal, out, 2) == -1, "capacity bound");
        // Synthetic safe filesystem (no Nintendo data), with one root file.
        std::vector<uint8_t> rom(0x240);
        auto word = [&](size_t at, uint32_t value) { for (int b=0;b<4;++b) rom[at+b]=value>>(b*8); };
        word(0x40,0x200); word(0x44,16); word(0x48,0x210); word(0x4c,8);
        word(0x200,8); rom[0x206]=1; rom[0x208]=1; rom[0x209]='a';
        word(0x210,0x220); word(0x214,0x223); rom[0x220]='a';rom[0x221]='b';rom[0x222]='c';
        std::string temp = argc > 2 ? argv[2] : "/tmp/sm64ds-fs-test.nds";
        auto save = [&] { std::ofstream f(temp,std::ios::binary); f.write(reinterpret_cast<char*>(rom.data()),rom.size()); };
        save(); check(ntr::fs_mount_rom(temp.c_str()),"synthetic mount");
        check(ntr::fs_id_by_path("a")==0,"file name index");
        check(ntr::fs_read(0,out,-1)==-1,"negative read capacity");
        check(ntr::fs_read(0,out,3)==3 && out[2]=='c',"file read");
        word(0x40,0xfffffff0); save(); check(!ntr::fs_mount_rom(temp.c_str()),"overflow FNT");
        check(!ntr::fs_mounted() && ntr::fs_file_count()==0,"failed mount clears state");
        word(0x40,0x200); word(0x214,0xffffffff); save(); check(!ntr::fs_mount_rom(temp.c_str()),"invalid FAT extent");
        word(0x214,0x223); word(0x44,4); save();check(!ntr::fs_mount_rom(temp.c_str()),"short FNT");
        word(0x44,16); rom[0x208]=2;rom[0x209]='.';rom[0x20a]='.';save();check(!ntr::fs_mount_rom(temp.c_str()),"unsafe name");
        rom[0x208]=0x81;rom[0x209]='d';rom[0x20a]=0;rom[0x20b]=0xf0;save();check(!ntr::fs_mount_rom(temp.c_str()),"directory cycle");
        std::remove(temp.c_str());
        if (argc > 1) std::puts(sm64ds::probe_rom(argv[1]).c_str());
        std::puts("native ROM tests passed");
    } catch (const std::exception &e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
