
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2019 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ArchiveManager.cpp
// Description: ArchiveManager class. Manages all open archives and the
//              interactions between them
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "ArchiveManager.h"
#include "App.h"
#include "Formats/All.h"
#include "Formats/DirArchive.h"
#include "General/Console/Console.h"
#include "General/Database.h"
#include "General/ResourceManager.h"
#include "General/UI.h"
#include "Utility/DateTime.h"
#include "Utility/FileUtils.h"
#include "Utility/StringUtils.h"


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
CVAR(Int, base_resource, -1, CVar::Flag::Save)
CVAR(Bool, auto_open_wads_root, false, CVar::Flag::Save)


// -----------------------------------------------------------------------------
//
// ArchiveManager Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// ArchiveManager class destructor
// -----------------------------------------------------------------------------
ArchiveManager::~ArchiveManager()
{
	clearAnnouncers();
}

// -----------------------------------------------------------------------------
// Checks that the given directory is actually a suitable resource directory
// for SLADE 3, and not just a directory named 'res' that happens to be there
// (possibly because the user installed SLADE in the same folder as an
// installation of SLumpEd).
// -----------------------------------------------------------------------------
bool ArchiveManager::validResDir(string_view dir) const
{
	// Assortment of resources that the program expects to find.
	// If at least one is missing, then probably more are missing
	// too, so the res folder cannot be used.
	static string paths[] = {
		"animated.lmp",
		"config/executables.cfg",
		"config/nodebuilders.cfg",
		"fonts/dejavu_sans.ttf",
		"html/box-title-back.png",
		"html/startpage.htm",
		"icons/entry_list/archive.png",
		"icons/general/wiki.png",
		"images/arrow.png",
		"logo.png",
		"palettes/Doom .pal",
		"s3dummy.lmp",
		"slade.ico",
		"switches.lmp",
		"tips.txt",
		"vga-rom-font.16",
	};

	for (const auto& path : paths)
	{
		if (!FileUtil::fileExists(fmt::format("{}/{}", dir, path)))
		{
			Log::warning(
				"Resource {} was not found in dir {}!\n"
				"This resource folder cannot be used. "
				"(Did you install SLADE 3 in a SLumpEd folder?)",
				path,
				dir);
			return false;
		}
	}
	return true;
}

// -----------------------------------------------------------------------------
// Initialised the ArchiveManager. Finds and opens the program resource archive
// -----------------------------------------------------------------------------
bool ArchiveManager::init()
{
	program_resource_archive_ = std::make_unique<ZipArchive>();

#ifdef __WXOSX__
	auto resdir = App::path("../Resources", App::Dir::Executable); // Use Resources dir within bundle on mac
#else
	auto resdir = App::path("res", App::Dir::Executable);
#endif

	if (FileUtil::dirExists(resdir) && validResDir(resdir))
	{
		program_resource_archive_->importDir(resdir);
		res_archive_open_ = (program_resource_archive_->numEntries() > 0);

		if (!initArchiveFormats())
			Log::error("An error occurred reading archive formats configuration");

		return res_archive_open_;
	}

	// Find slade3.pk3 directory
	auto dir_slade_pk3 = App::path("slade.pk3", App::Dir::Resources);
	if (!FileUtil::fileExists(dir_slade_pk3))
		dir_slade_pk3 = App::path("slade.pk3", App::Dir::Data);
	if (!FileUtil::fileExists(dir_slade_pk3))
		dir_slade_pk3 = App::path("slade.pk3", App::Dir::Executable);
	if (!FileUtil::fileExists(dir_slade_pk3))
		dir_slade_pk3 = App::path("slade.pk3", App::Dir::User);
	if (!FileUtil::fileExists(dir_slade_pk3))
		dir_slade_pk3 = "slade.pk3";

	// Open slade.pk3
	if (!program_resource_archive_->open(dir_slade_pk3))
	{
		Log::error("Unable to find slade.pk3!");
		res_archive_open_ = false;
	}
	else
		res_archive_open_ = true;

	if (!initArchiveFormats())
		Log::error("An error occurred reading archive formats configuration");

	return res_archive_open_;
}

// -----------------------------------------------------------------------------
// Loads archive formats configuration from the program resource
// -----------------------------------------------------------------------------
bool ArchiveManager::initArchiveFormats() const
{
	return Archive::loadFormats(program_resource_archive_->entryAtPath("config/archive_formats.cfg")->data());
}

// -----------------------------------------------------------------------------
// Initialises the base resource archive
// -----------------------------------------------------------------------------
bool ArchiveManager::initBaseResource()
{
	return openBaseResource((int)base_resource);
}

// -----------------------------------------------------------------------------
// Adds an archive to the archive list
// -----------------------------------------------------------------------------
bool ArchiveManager::addArchive(shared_ptr<Archive> archive)
{
	// Only add if archive is a valid pointer
	if (archive)
	{
		// Add to the list
		OpenArchive n_archive;
		n_archive.archive  = archive;
		n_archive.resource = true;
		open_archives_.push_back(n_archive);

		// Listen to the archive
		listenTo(archive.get());

		// Announce the addition
		announce("archive_added");

		// Add to resource manager
		App::resources().addArchive(archive.get());

		// ZDoom also loads any WADs found in the root of a PK3 or directory
		if ((archive->formatId() == "zip" || archive->formatId() == "folder") && auto_open_wads_root)
		{
			for (const auto& entry : archive->rootDir()->allEntries())
			{
				if (entry->type() == EntryType::unknownType())
					EntryType::detectEntryType(*entry);

				if (entry->type()->id() == "wad")
					// First true: yes, manage this
					// Second true: open silently, don't open a tab for it
					openArchive(entry.get(), true, true);
			}
		}

		return true;
	}
	else
		return false;
}

// -----------------------------------------------------------------------------
// Returns the archive at the index specified (nullptr if it doesn't exist)
// -----------------------------------------------------------------------------
shared_ptr<Archive> ArchiveManager::getArchive(int index)
{
	// Check that index is valid
	if (index < 0 || index >= (int)open_archives_.size())
		return nullptr;
	else
		return open_archives_[index].archive;
}

// -----------------------------------------------------------------------------
// Returns the archive with the specified filename
// (nullptr if it doesn't exist)
// -----------------------------------------------------------------------------
shared_ptr<Archive> ArchiveManager::getArchive(string_view filename)
{
	// Go through all open archives
	for (auto& open_archive : open_archives_)
	{
		// If the filename matches, return it
		if (open_archive.archive->filename() == filename)
			return open_archive.archive;
	}

	// If no archive is found with a matching filename, return nullptr
	return nullptr;
}

// -----------------------------------------------------------------------------
// Opens and adds a archive to the list, returns a pointer to the newly opened
// and added archive, or nullptr if an error occurred
// -----------------------------------------------------------------------------
shared_ptr<Archive> ArchiveManager::openArchive(string_view filename, bool manage, bool silent)
{
	// Check for directory
	if (FileUtil::dirExists(filename))
		return openDirArchive(filename, manage, silent);

	auto new_archive = getArchive(filename);

	Log::info("Opening archive {}", filename);

	// If the archive is already open, just return it
	if (new_archive)
	{
		// Announce open
		if (!silent)
		{
			MemChunk mc;
			uint32_t index = archiveIndex(new_archive.get());
			mc.write(&index, 4);
			announce("archive_opened", mc);
		}

		return new_archive;
	}

	// Determine file format
	string std_fn{ filename };
	if (WadArchive::isWadArchive(std_fn))
		new_archive = std::make_shared<WadArchive>();
	else if (ZipArchive::isZipArchive(std_fn))
		new_archive = std::make_shared<ZipArchive>();
	else if (ResArchive::isResArchive(std_fn))
		new_archive = std::make_shared<ResArchive>();
	else if (DatArchive::isDatArchive(std_fn))
		new_archive = std::make_shared<DatArchive>();
	else if (LibArchive::isLibArchive(std_fn))
		new_archive = std::make_shared<LibArchive>();
	else if (PakArchive::isPakArchive(std_fn))
		new_archive = std::make_shared<PakArchive>();
	else if (BSPArchive::isBSPArchive(std_fn))
		new_archive = std::make_shared<BSPArchive>();
	else if (GrpArchive::isGrpArchive(std_fn))
		new_archive = std::make_shared<GrpArchive>();
	else if (RffArchive::isRffArchive(std_fn))
		new_archive = std::make_shared<RffArchive>();
	else if (GobArchive::isGobArchive(std_fn))
		new_archive = std::make_shared<GobArchive>();
	else if (LfdArchive::isLfdArchive(std_fn))
		new_archive = std::make_shared<LfdArchive>();
	else if (HogArchive::isHogArchive(std_fn))
		new_archive = std::make_shared<HogArchive>();
	else if (ADatArchive::isADatArchive(std_fn))
		new_archive = std::make_shared<ADatArchive>();
	else if (Wad2Archive::isWad2Archive(std_fn))
		new_archive = std::make_shared<Wad2Archive>();
	else if (WadJArchive::isWadJArchive(std_fn))
		new_archive = std::make_shared<WadJArchive>();
	else if (WolfArchive::isWolfArchive(std_fn))
		new_archive = std::make_shared<WolfArchive>();
	else if (GZipArchive::isGZipArchive(std_fn))
		new_archive = std::make_shared<GZipArchive>();
	else if (BZip2Archive::isBZip2Archive(std_fn))
		new_archive = std::make_shared<BZip2Archive>();
	else if (TarArchive::isTarArchive(std_fn))
		new_archive = std::make_shared<TarArchive>();
	else if (DiskArchive::isDiskArchive(std_fn))
		new_archive = std::make_shared<DiskArchive>();
	else if (PodArchive::isPodArchive(std_fn))
		new_archive = std::make_shared<PodArchive>();
	else if (ChasmBinArchive::isChasmBinArchive(std_fn))
		new_archive = std::make_shared<ChasmBinArchive>();
	else if (SiNArchive::isSiNArchive(std_fn))
		new_archive = std::make_shared<SiNArchive>();
	else
	{
		// Unsupported format
		Global::error = "Unsupported or invalid Archive format";
		return nullptr;
	}

	// If it opened successfully, add it to the list if needed & return it,
	// Otherwise, delete it and return nullptr
	if (new_archive->open(filename))
	{
		if (manage)
		{
			// Add the archive
			addArchive(new_archive);

			// Add/update in database
			addOrUpdateArchiveDB(filename, *new_archive);

			// Announce open
			if (!silent)
			{
				MemChunk mc;
				uint32_t index = archiveIndex(new_archive.get());
				mc.write(&index, 4);
				announce("archive_opened", mc);
			}
		}

		// Return the opened archive
		return new_archive;
	}
	else
	{
		Log::error(Global::error);
		return nullptr;
	}
}

// -----------------------------------------------------------------------------
// Same as the above function, except it opens from an ArchiveEntry
// -----------------------------------------------------------------------------
shared_ptr<Archive> ArchiveManager::openArchive(ArchiveEntry* entry, bool manage, bool silent)
{
	// Check entry was given
	if (!entry)
		return nullptr;

	// Check if the entry is already opened
	for (auto& open_archive : open_archives_)
	{
		if (open_archive.archive->parentEntry() == entry)
		{
			// Announce open
			if (!silent)
			{
				MemChunk mc;
				uint32_t index = archiveIndex(open_archive.archive.get());
				mc.write(&index, 4);
				announce("archive_opened", mc);
			}

			return open_archive.archive;
		}
	}

	// Check entry type
	shared_ptr<Archive> new_archive;
	if (WadArchive::isWadArchive(entry->data()))
		new_archive = std::make_shared<WadArchive>();
	else if (ZipArchive::isZipArchive(entry->data()))
		new_archive = std::make_shared<ZipArchive>();
	else if (ResArchive::isResArchive(entry->data()))
		new_archive = std::make_shared<ResArchive>();
	else if (LibArchive::isLibArchive(entry->data()))
		new_archive = std::make_shared<LibArchive>();
	else if (DatArchive::isDatArchive(entry->data()))
		new_archive = std::make_shared<DatArchive>();
	else if (PakArchive::isPakArchive(entry->data()))
		new_archive = std::make_shared<PakArchive>();
	else if (BSPArchive::isBSPArchive(entry->data()))
		new_archive = std::make_shared<BSPArchive>();
	else if (GrpArchive::isGrpArchive(entry->data()))
		new_archive = std::make_shared<GrpArchive>();
	else if (RffArchive::isRffArchive(entry->data()))
		new_archive = std::make_shared<RffArchive>();
	else if (GobArchive::isGobArchive(entry->data()))
		new_archive = std::make_shared<GobArchive>();
	else if (LfdArchive::isLfdArchive(entry->data()))
		new_archive = std::make_shared<LfdArchive>();
	else if (HogArchive::isHogArchive(entry->data()))
		new_archive = std::make_shared<HogArchive>();
	else if (ADatArchive::isADatArchive(entry->data()))
		new_archive = std::make_shared<ADatArchive>();
	else if (Wad2Archive::isWad2Archive(entry->data()))
		new_archive = std::make_shared<Wad2Archive>();
	else if (WadJArchive::isWadJArchive(entry->data()))
		new_archive = std::make_shared<WadJArchive>();
	else if (WolfArchive::isWolfArchive(entry->data()))
		new_archive = std::make_shared<WolfArchive>();
	else if (GZipArchive::isGZipArchive(entry->data()))
		new_archive = std::make_shared<GZipArchive>();
	else if (BZip2Archive::isBZip2Archive(entry->data()))
		new_archive = std::make_shared<BZip2Archive>();
	else if (TarArchive::isTarArchive(entry->data()))
		new_archive = std::make_shared<TarArchive>();
	else if (DiskArchive::isDiskArchive(entry->data()))
		new_archive = std::make_shared<DiskArchive>();
	else if (StrUtil::endsWithCI(entry->name(), ".pod") && PodArchive::isPodArchive(entry->data()))
		new_archive = std::make_shared<PodArchive>();
	else if (ChasmBinArchive::isChasmBinArchive(entry->data()))
		new_archive = std::make_shared<ChasmBinArchive>();
	else if (SiNArchive::isSiNArchive(entry->data()))
		new_archive = std::make_shared<SiNArchive>();
	else
	{
		// Unsupported format
		Global::error = "Unsupported or invalid Archive format";
		return nullptr;
	}

	// If it opened successfully, add it to the list & return it,
	// Otherwise, delete it and return nullptr
	if (new_archive->open(entry))
	{
		if (manage)
		{
			// Add to parent's child list if parent is open in the manager (it should be)
			int index_parent = -1;
			if (entry->parent())
				index_parent = archiveIndex(entry->parent());
			if (index_parent >= 0)
				open_archives_[index_parent].open_children.emplace_back(new_archive);

			// Add the new archive
			addArchive(new_archive);

			// Announce open
			if (!silent)
			{
				MemChunk mc;
				uint32_t index = archiveIndex(new_archive.get());
				mc.write(&index, 4);
				announce("archive_opened", mc);
			}
		}

		return new_archive;
	}
	else
	{
		Log::error(Global::error);
		return nullptr;
	}
}

// -----------------------------------------------------------------------------
// Opens [dir] as a DirArchive and adds it to the list.
// Returns a pointer to the archive or nullptr if an error occurred.
// -----------------------------------------------------------------------------
shared_ptr<Archive> ArchiveManager::openDirArchive(string_view dir, bool manage, bool silent)
{
	auto new_archive = getArchive(dir);

	Log::info("Opening directory {} as archive", dir);

	// If the archive is already open, just return it
	if (new_archive)
	{
		// Announce open
		if (!silent)
		{
			MemChunk mc;
			uint32_t index = archiveIndex(new_archive.get());
			mc.write(&index, 4);
			announce("archive_opened", mc);
		}

		return new_archive;
	}

	new_archive = std::make_shared<DirArchive>();

	// If it opened successfully, add it to the list if needed & return it,
	// Otherwise, delete it and return nullptr
	if (new_archive->open(dir))
	{
		if (manage)
		{
			// Add the archive
			addArchive(new_archive);

			// Add to recent files
			addOrUpdateArchiveDB(dir, *new_archive);

			// Announce open
			if (!silent)
			{
				MemChunk mc;
				uint32_t index = archiveIndex(new_archive.get());
				mc.write(&index, 4);
				announce("archive_opened", mc);
			}
		}

		// Return the opened archive
		return new_archive;
	}
	else
	{
		Log::error(Global::error);
		return nullptr;
	}
}

// -----------------------------------------------------------------------------
// Creates a new archive of the specified format and adds it to the list of open
// archives. Returns the created archive, or nullptr if an invalid archive type
// was given
// -----------------------------------------------------------------------------
shared_ptr<Archive> ArchiveManager::newArchive(string_view format)
{
	// Create a new archive depending on the type specified
	shared_ptr<Archive> new_archive;
	if (format == "wad")
		new_archive = std::make_shared<WadArchive>();
	else if (format == "zip")
		new_archive = std::make_shared<ZipArchive>();
	else
	{
		Global::error = fmt::format("Can not create archive of format: {}", format);
		Log::error(Global::error);
		return nullptr;
	}

	// If the archive was created, set its filename and add it to the list
	if (new_archive)
	{
		new_archive->setFilename(fmt::format("UNSAVED ({})", new_archive->formatDesc().name));
		addArchive(new_archive);
	}

	// Return the created archive, if any
	return new_archive;
}

// -----------------------------------------------------------------------------
// Closes the archive at index, and removes it from the list if the index is
// valid. Returns false on invalid index, true otherwise
// -----------------------------------------------------------------------------
bool ArchiveManager::closeArchive(int index)
{
	// Check for invalid index
	if (index < 0 || index >= (int)open_archives_.size())
		return false;

	// Announce archive closing
	MemChunk mc;
	int32_t  temp = index;
	mc.write(&temp, 4);
	announce("archive_closing", mc);

	// Delete any bookmarked entries contained in the archive
	deleteBookmarksInArchive(open_archives_[index].archive.get());

	// Remove from resource manager
	App::resources().removeArchive(open_archives_[index].archive.get());

	// Close any open child archives
	// Clear out the open_children vector first, lest the children try to remove themselves from it
	auto open_children = open_archives_[index].open_children;
	open_archives_[index].open_children.clear();
	for (auto& archive : open_children)
	{
		int ci = archiveIndex(archive.lock().get());
		if (ci >= 0)
			closeArchive(ci);
	}

	// Remove ourselves from our parent's open-child list
	auto parent = open_archives_[index].archive->parentEntry();
	if (parent)
	{
		auto gp = parent->parent();
		if (gp)
		{
			int pi = archiveIndex(gp);
			if (pi >= 0)
			{
				auto& children = open_archives_[pi].open_children;
				for (auto it = children.begin(); it < children.end(); ++it)
				{
					if (it->lock() == open_archives_[index].archive)
					{
						children.erase(it, it + 1);
						break;
					}
				}
			}
		}
	}

	// Close the archive
	open_archives_[index].archive->close();

	// Remove the archive at index from the list
	open_archives_.erase(open_archives_.begin() + index);

	// Announce closed
	announce("archive_closed", mc);

	return true;
}

// -----------------------------------------------------------------------------
// Finds the archive with a matching filename, deletes it and removes it from
// the list.
// Returns false if it doesn't exist or can't be removed, true otherwise
// -----------------------------------------------------------------------------
bool ArchiveManager::closeArchive(string_view filename)
{
	// Go through all open archives
	for (int a = 0; a < (int)open_archives_.size(); a++)
	{
		// If the filename matches, remove it
		if (open_archives_[a].archive->filename() == filename)
			return closeArchive(a);
	}

	// If no archive is found with a matching filename, return false
	return false;
}

// -----------------------------------------------------------------------------
// Closes the specified archive and removes it from the list, if it exists in
// the list. Returns false if it doesn't exist, else true
// -----------------------------------------------------------------------------
bool ArchiveManager::closeArchive(Archive* archive)
{
	// Go through all open archives
	for (int a = 0; a < (int)open_archives_.size(); a++)
	{
		// If the archive exists in the list, remove it
		if (open_archives_[a].archive.get() == archive)
			return closeArchive(a);
	}

	// If the archive isn't in the list, return false
	return false;
}

// -----------------------------------------------------------------------------
// Closes all opened archives
// -----------------------------------------------------------------------------
void ArchiveManager::closeAll()
{
	// Close the first archive in the list until no archives are open
	while (!open_archives_.empty())
		closeArchive(0);
}

// -----------------------------------------------------------------------------
// Returns the index in the list of the given archive, or -1 if the archive
// doesn't exist in the list
// -----------------------------------------------------------------------------
int ArchiveManager::archiveIndex(Archive* archive)
{
	// Go through all open archives
	for (size_t a = 0; a < open_archives_.size(); a++)
	{
		// If the archive we're looking for is this one, return the index
		if (open_archives_[a].archive.get() == archive)
			return (int)a;
	}

	// If we get to here the archive wasn't found, so return -1
	return -1;
}

// -----------------------------------------------------------------------------
// Returns all open archives that live inside this one, recursively.
// -----------------------------------------------------------------------------
// This is the recursive bit, separate from the public entry point
void ArchiveManager::getDependentArchivesInternal(Archive* archive, vector<shared_ptr<Archive>>& vec)
{
	int ai = archiveIndex(archive);

	for (auto& child : open_archives_[ai].open_children)
	{
		if (auto child_sptr = child.lock())
		{
			vec.push_back(child_sptr);
			getDependentArchivesInternal(child_sptr.get(), vec);
		}
	}
}
vector<shared_ptr<Archive>> ArchiveManager::getDependentArchives(Archive* archive)
{
	vector<shared_ptr<Archive>> vec;
	getDependentArchivesInternal(archive, vec);
	return vec;
}

// -----------------------------------------------------------------------------
// Adds (or updates) the given [archive] at [file_path] in the database
// -----------------------------------------------------------------------------
void ArchiveManager::addOrUpdateArchiveDB(string_view file_path, const Archive& archive) const
{
	if (auto sql = Database::global().getOrCreateCachedQuery(
			"am_insert_archive_file",
			"REPLACE INTO archive_file (path, size, md5, format_id, last_opened, last_modified) "
			"VALUES (?,?,?,?,?,?)",
			true))
	{
		sql->clearBindings();

		if (archive.formatId() != "folder")
		{
			// Regular archive
			SFile file(file_path);
			sql->bind(2, file.size());
			sql->bind(3, file.calculateMD5());
			sql->bind(6, FileUtil::fileModifiedTime(file_path));
		}
		else
		{
			// Directory
			sql->bind(2, 0);
			sql->bind(3, "");
			sql->bind(6, 0);
		}

		sql->bind(1, string{ file_path });
		sql->bind(4, archive.formatId());
		sql->bind(5, DateTime::now());
		sql->exec();
		sql->reset();
	}
}

// -----------------------------------------------------------------------------
// Returns a string containing the extensions of all supported archive formats,
// that can be used for wxWidgets file dialogs
// -----------------------------------------------------------------------------
string ArchiveManager::getArchiveExtensionsString() const
{
	auto           formats = Archive::allFormats();
	vector<string> ext_strings;
	string         ext_all = "Any supported file|";
	for (const auto& fmt : formats)
	{
		for (const auto& ext : fmt.extensions)
		{
			auto ext_case = fmt::format("*.{};", StrUtil::lower(ext.first));
			ext_case += fmt::format("*.{};", StrUtil::upper(ext.first));
			ext_case += fmt::format("*.{}", StrUtil::capitalize(ext.first));

			ext_all += fmt::format("{};", ext_case);
			ext_strings.push_back(fmt::format("{} files (*.{})|{}", ext.second, ext.first, ext_case));
		}
	}

	ext_all.pop_back();
	for (const auto& ext : ext_strings)
	{
		ext_all += '|';
		ext_all += ext;
	}

	return ext_all;
}

// -----------------------------------------------------------------------------
// Returns true if [archive] is set to be used as a resource, false otherwise
// -----------------------------------------------------------------------------
bool ArchiveManager::archiveIsResource(Archive* archive)
{
	int index = archiveIndex(archive);
	if (index < 0)
		return false;
	else
		return open_archives_[index].resource;
}

// -----------------------------------------------------------------------------
// Sets/unsets [archive] to be used as a resource
// -----------------------------------------------------------------------------
void ArchiveManager::setArchiveResource(Archive* archive, bool resource)
{
	int index = archiveIndex(archive);
	if (index >= 0)
	{
		bool was_resource              = open_archives_[index].resource;
		open_archives_[index].resource = resource;

		// Update resource manager
		if (resource && !was_resource)
			App::resources().addArchive(archive);
		else if (!resource && was_resource)
			App::resources().removeArchive(archive);
	}
}

// -----------------------------------------------------------------------------
// Returns a vector of all open archives.
// If [resources_only] is true, only includes archives marked as resources
// -----------------------------------------------------------------------------
vector<shared_ptr<Archive>> ArchiveManager::allArchives(bool resource_only)
{
	vector<shared_ptr<Archive>> ret;

	for (const auto& archive : open_archives_)
		if (!resource_only || archive.resource)
			ret.push_back(archive.archive);

	return ret;
}

// -----------------------------------------------------------------------------
// Returns a shared_ptr to the given [archive], or nullptr if it isn't open in
// the archive manager
// -----------------------------------------------------------------------------
shared_ptr<Archive> ArchiveManager::shareArchive(Archive* const archive)
{
	for (const auto& oa : open_archives_)
		if (oa.archive.get() == archive)
			return oa.archive;

	return nullptr;
}

// -----------------------------------------------------------------------------
// Returns a list of the [count] most recently opened files
// -----------------------------------------------------------------------------
vector<string> ArchiveManager::recentFiles(int count) const
{
	vector<string> paths;

	// Get or create cached query to select base resource paths
	if (auto sql = Database::global().getOrCreateCachedQuery(
			"am_list_recent_files", "SELECT path FROM archive_file ORDER BY last_opened DESC LIMIT ?"))
	{
		sql->bind(1, count);

		// Execute query and add results to list
		while (sql->executeStep())
			paths.push_back(sql->getColumn(0).getString());

		sql->reset();
	}

	return paths;
}

// -----------------------------------------------------------------------------
// Returns a list of all base resource archive paths
// -----------------------------------------------------------------------------
vector<string> ArchiveManager::baseResourcePaths() const
{
	vector<string> paths;

	// Get or create cached query to select base resource paths
	if (auto sql = Database::global().getOrCreateCachedQuery("am_list_br_paths", "SELECT path FROM base_resource_path"))
	{
		// Execute query and add results to list
		while (sql->executeStep())
			paths.push_back(sql->getColumn(0).getString());

		sql->reset();
	}

	return paths;
}

// -----------------------------------------------------------------------------
// Adds [path] to the list of base resource paths
// -----------------------------------------------------------------------------
bool ArchiveManager::addBaseResourcePath(string_view path)
{
	// Firstly, check the file exists
	if (!FileUtil::fileExists(path))
		return false;

	// Get or create cached query to add base resource paths
	auto sql = Database::global().getOrCreateCachedQuery(
		"am_insert_br_path", "INSERT OR IGNORE INTO base_resource_path (path) VALUES (?)", true);
	if (!sql)
		return false;

	// Add path to database (if it doesn't already exist)
	sql->bind(1, string{ path });
	auto result = sql->exec();
	sql->reset();

	if (!result)
		return false;

	// Announce
	announce("base_resource_path_added");

	return true;
}

// -----------------------------------------------------------------------------
// Removes the base resource path at [index]
// -----------------------------------------------------------------------------
void ArchiveManager::removeBaseResourcePath(unsigned index)
{
	// Unload base resource if removed is open
	if (index == (unsigned)base_resource)
		openBaseResource(-1);

	// Modify base_resource cvar if needed
	else if (base_resource > (signed)index)
		base_resource = base_resource - 1;

	// Remove the path
	if (Database::global().exec(fmt::format("DELETE FROM base_resource_path WHERE rowid = {}", index + 1)) > 0)
		announce("base_resource_path_removed");
}

// -----------------------------------------------------------------------------
// Returns the number of base resource archive paths in the database
// -----------------------------------------------------------------------------
unsigned ArchiveManager::numBaseResourcePaths() const
{
	return Database::connectionRO()->execAndGet("SELECT COUNT(*) FROM base_resource_path").getInt();
}

// -----------------------------------------------------------------------------
// Returns the base resource path at [index]
// -----------------------------------------------------------------------------
string ArchiveManager::getBaseResourcePath(unsigned index)
{
	auto db = Database::connectionRO();
	if (!db)
		return {};

	SQLite::Statement sql_br_path(*db, fmt::format("SELECT path FROM base_resource_path WHERE rowid = {}", index + 1));
	if (sql_br_path.executeStep())
		return sql_br_path.getColumn(0).getString();

	return {};
}

// -----------------------------------------------------------------------------
// Opens the base resource archive [index]
// -----------------------------------------------------------------------------
bool ArchiveManager::openBaseResource(int index)
{
	// Check we're opening a different archive
	if (base_resource_archive_ && base_resource == index)
		return true;

	// Close/delete current base resource archive
	if (base_resource_archive_)
	{
		App::resources().removeArchive(base_resource_archive_.get());
		base_resource_archive_ = nullptr;
	}

	// Get base resource path at [index]
	string filename;
	if (index >= 0)
		filename = getBaseResourcePath(index);

	// Check index was valid
	if (filename.empty())
	{
		base_resource = -1;
		announce("base_resource_changed");
		return false;
	}

	// Create archive based on file type
	if (WadArchive::isWadArchive(filename))
		base_resource_archive_ = std::make_unique<WadArchive>();
	else if (ZipArchive::isZipArchive(filename))
		base_resource_archive_ = std::make_unique<ZipArchive>();
	else
		return false;

	// Attempt to open the file
	UI::showSplash(fmt::format("Opening {}...", filename), true);
	if (base_resource_archive_->open(filename))
	{
		base_resource = index;
		UI::hideSplash();
		App::resources().addArchive(base_resource_archive_.get());
		announce("base_resource_changed");
		return true;
	}
	base_resource_archive_ = nullptr;
	UI::hideSplash();
	announce("base_resource_changed");
	return false;
}

// -----------------------------------------------------------------------------
// Returns the first entry matching [name] in the resource archives.
// Resource archives = open archives -> base resource archives.
// -----------------------------------------------------------------------------
ArchiveEntry* ArchiveManager::getResourceEntry(string_view name, Archive* ignore)
{
	// Go through all open archives
	for (auto& open_archive : open_archives_)
	{
		// If it isn't a resource archive, skip it
		if (!open_archive.resource)
			continue;

		// If it's being ignored, skip it
		if (open_archive.archive.get() == ignore)
			continue;

		// Try to find the entry in the archive
		auto entry = open_archive.archive->entry(name);
		if (entry)
			return entry;
	}

	// If entry isn't found yet, search the base resource archive
	if (base_resource_archive_)
		return base_resource_archive_->entry(name);

	return nullptr;
}

// -----------------------------------------------------------------------------
// Searches for an entry matching [options] in the resource archives
// -----------------------------------------------------------------------------
ArchiveEntry* ArchiveManager::findResourceEntry(Archive::SearchOptions& options, Archive* ignore)
{
	// Go through all open archives
	for (auto& open_archive : open_archives_)
	{
		// If it isn't a resource archive, skip it
		if (!open_archive.resource)
			continue;

		// If it's being ignored, skip it
		if (open_archive.archive.get() == ignore)
			continue;

		// Try to find the entry in the archive
		auto entry = open_archive.archive->findLast(options);
		if (entry)
			return entry;
	}

	// If entry isn't found yet, search the base resource archive
	if (base_resource_archive_)
		return base_resource_archive_->findLast(options);

	return nullptr;
}

// -----------------------------------------------------------------------------
// Searches for entries matching [options] in the resource archives
// -----------------------------------------------------------------------------
vector<ArchiveEntry*> ArchiveManager::findAllResourceEntries(Archive::SearchOptions& options, Archive* ignore)
{
	vector<ArchiveEntry*> ret;

	// Search the base resource archive first
	if (base_resource_archive_)
	{
		auto vec = base_resource_archive_->findAll(options);
		ret.insert(ret.end(), vec.begin(), vec.end());
	}

	// Go through all open archives
	for (auto& open_archive : open_archives_)
	{
		// If it isn't a resource archive, skip it
		if (!open_archive.resource)
			continue;

		// If it's being ignored, skip it
		if (open_archive.archive.get() == ignore)
			continue;

		// Add matching entries from this archive
		auto vec = open_archive.archive->findAll(options);
		ret.insert(ret.end(), vec.begin(), vec.end());
	}

	return ret;
}

// -----------------------------------------------------------------------------
// Adds [entry] to the bookmark list
// -----------------------------------------------------------------------------
void ArchiveManager::addBookmark(const shared_ptr<ArchiveEntry>& entry)
{
	// Check the bookmark isn't already in the list
	for (auto& bookmark : bookmarks_)
	{
		if (bookmark.lock() == entry)
			return;
	}

	// Add bookmark
	bookmarks_.push_back(entry);

	// Announce
	announce("bookmarks_changed");
}

// -----------------------------------------------------------------------------
// Removes [entry] from the bookmarks list
// -----------------------------------------------------------------------------
bool ArchiveManager::deleteBookmark(ArchiveEntry* entry)
{
	// Find bookmark to remove
	for (unsigned a = 0; a < bookmarks_.size(); a++)
	{
		if (bookmarks_[a].lock().get() == entry)
		{
			// Remove it
			bookmarks_.erase(bookmarks_.begin() + a);

			// Announce
			announce("bookmarks_changed");

			return true;
		}
	}

	// Entry not in bookmarks list
	return false;
}

// -----------------------------------------------------------------------------
// Removes the bookmarked entry [index]
// -----------------------------------------------------------------------------
bool ArchiveManager::deleteBookmark(unsigned index)
{
	// Check index
	if (index >= bookmarks_.size())
		return false;

	// Remove bookmark
	bookmarks_.erase(bookmarks_.begin() + index);

	// Announce
	announce("bookmarks_changed");

	return true;
}

// -----------------------------------------------------------------------------
// Removes any bookmarked entries in [archive] from the list
// -----------------------------------------------------------------------------
bool ArchiveManager::deleteBookmarksInArchive(Archive* archive)
{
	// Go through bookmarks
	bool removed = false;
	for (unsigned a = 0; a < bookmarks_.size(); a++)
	{
		// Check bookmarked entry's parent archive
		auto bookmark = bookmarks_[a].lock();
		if (!bookmark || bookmark->parent() == archive)
		{
			bookmarks_.erase(bookmarks_.begin() + a);
			a--;
			removed = true;
		}
	}

	if (removed)
	{
		// Announce
		announce("bookmarks_changed");
		return true;
	}
	else
		return false;
}

// -----------------------------------------------------------------------------
// Removes any bookmarked entries in [node] from the list
// -----------------------------------------------------------------------------
bool ArchiveManager::deleteBookmarksInDir(ArchiveDir* node)
{
	// Go through bookmarks
	auto archive = node->archive();
	bool removed = deleteBookmark(node->dirEntry());
	for (unsigned a = 0; a < bookmarks_.size(); ++a)
	{
		// Check bookmarked entry's parent archive
		auto bookmark = bookmarks_[a].lock();
		if (!bookmark || bookmark->parent() == archive)
		{
			bool remove = true;

			// Now check if the bookmarked entry is within
			// the removed dir or one of its descendants
			if (bookmark)
			{
				remove     = false;
				auto anode = bookmark->parentDir();
				while (anode != archive->rootDir().get() && !remove)
				{
					if (anode == node)
						remove = true;
					else
						anode = anode->parent().get();
				}
			}

			if (remove)
			{
				bookmarks_.erase(bookmarks_.begin() + a);
				--a;
				removed = true;
			}
		}
	}

	if (removed)
	{
		// Announce
		announce("bookmarks_changed");
		return true;
	}
	else
		return false;
}

// -----------------------------------------------------------------------------
// Returns the bookmarked entry at [index]
// -----------------------------------------------------------------------------
ArchiveEntry* ArchiveManager::getBookmark(unsigned index)
{
	// Check index
	if (index >= bookmarks_.size())
		return nullptr;

	return bookmarks_[index].lock().get();
}

// -----------------------------------------------------------------------------
// Called when an announcement is recieved from one of the archives in the list
// -----------------------------------------------------------------------------
void ArchiveManager::onAnnouncement(Announcer* announcer, string_view event_name, MemChunk& event_data)
{
	// Reset event data for reading
	event_data.seek(0, SEEK_SET);

	// Check that the announcement came from an archive in the list
	auto    archive = dynamic_cast<Archive*>(announcer);
	int32_t index   = archiveIndex(archive);
	if (index >= 0)
	{
		// If the archive was saved
		if (event_name == "saved")
		{
			addOrUpdateArchiveDB(archive->filename(), *archive);

			MemChunk mc;
			mc.write(&index, 4);
			announce("archive_saved", mc);
		}

		// If the archive was modified
		if (event_name == "modified" || event_name == "entry_modified")
		{
			MemChunk mc;
			mc.write(&index, 4);
			announce("archive_modified", mc);
		}
	}
}


// -----------------------------------------------------------------------------
//
// Console Commands
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Lists the filenames of all open archives
// -----------------------------------------------------------------------------
CONSOLE_COMMAND(list_archives, 0, true)
{
	Log::info("{} Open Archives:", App::archiveManager().numArchives());

	for (int a = 0; a < App::archiveManager().numArchives(); a++)
	{
		auto archive = App::archiveManager().getArchive(a);
		Log::info("{}: \"{}\"", a + 1, archive->filename());
	}
}

// -----------------------------------------------------------------------------
// Attempts to open each given argument (filenames)
// -----------------------------------------------------------------------------
void c_open(const vector<string>& args)
{
	for (const auto& arg : args)
		App::archiveManager().openArchive(arg);
}
ConsoleCommand am_open("open", &c_open, 1, true); // Can't use the macro with this name
