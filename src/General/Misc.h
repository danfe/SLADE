#pragma once

class SImage;
class Archive;
class ArchiveEntry;
class Palette;
class Tokenizer;

namespace Misc
{
bool loadImageFromEntry(SImage* image, ArchiveEntry* entry, int index = 0);

// Palette detection
namespace PaletteHack
{
	static const int NONE      = 0;
	static const int ALPHA     = 1;
	static const int HERETIC   = 2;
	static const int SHADOW    = 3;
	static const int ROTT_N    = 4;
	static const int ROTT_D    = 5;
	static const int ROTT_F    = 6;
	static const int ROTT_A    = 7;
	static const int SOD_ID    = 8;
	static const int SOD_TITLE = 9;
	static const int SOD_END   = 10;
}; // namespace PaletteHack
int  detectPaletteHack(ArchiveEntry* entry);
bool loadPaletteFromArchive(Palette* pal, Archive* archive, int lump = PaletteHack::NONE);

string   sizeAsString(uint32_t size);
string   lumpNameToFileName(string_view lump);
string   fileNameToLumpName(string_view file);
uint32_t crc(const uint8_t* buf, uint32_t len);
Vec2i    findJaguarTextureDimensions(ArchiveEntry* entry, string_view name);

// Mass Rename
string massRenameFilter(vector<string>& names);
void   doMassRename(vector<string>& names, string_view name_filter);

// Dialog/Window sizes
struct WindowInfo
{
	string id;
	int    width, height, left, top;
	WindowInfo(string_view id, int w = -1, int h = -1, int l = -1, int t = -1) :
		id{ id },
		width{ w },
		height{ h },
		left{ l },
		top{ t }
	{
	}
};
WindowInfo getWindowInfo(string_view id);
void       setWindowInfo(string_view id, int width, int height, int left, int top);
} // namespace Misc
