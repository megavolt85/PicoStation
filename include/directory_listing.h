#pragma once

#include <stddef.h>
#include <stdint.h>

namespace picostation {
class DirectoryListing {
  public:
    
	static void init();
	static void gotoRoot();
    static bool gotoDirectory(const uint32_t index);
    static bool getPath(const uint32_t index, char* filePath);
	static void gotoParentDirectory();
    static bool getDirectoryEntries(const uint32_t offset);
	static uint16_t getDirectoryEntriesCount();
	static uint16_t* getFileListingData();
    
  private:
	static void combinePaths(const char* filePath1, const char* filePath2, char* newPath);
	static bool getDirectoryEntry(const uint32_t index, char* filePath);
};
}  // namespace picostation

