
#include "Main.h"
#include "Library.h"
#include "Archive/Archive.h"
#include "General/Database.h"
#include "Utility/DateTime.h"
#include "Utility/FileUtils.h"

using namespace slade;
using namespace library;

namespace slade::library
{
sigslot::signal<> signal_updated;
}

void library::addOrUpdateArchive(string_view file_path, const Archive& archive, database::Context* db)
{
	if (!db)
		db = &database::global();

	if (auto sql = db->cacheQuery(
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
			sql->bind(6, fileutil::fileModifiedTime(file_path));
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
		sql->bind(5, datetime::now());
		sql->exec();
		sql->reset();

		signal_updated();
	}
}

vector<string> library::recentFiles(unsigned count, database::Context* db)
{
	if (!db)
		db = &database::global();

	vector<string> paths;

	// Get or create cached query to select base resource paths
	if (auto sql = db->cacheQuery(
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

int library::archiveFileId(const Archive& archive, database::Context* db)
{
	if (!db)
		db = &database::global();

	int id = 0;

	if (auto sql = db->cacheQuery("am_get_archive_id", "SELECT id FROM archive_file WHERE path = ?"))
	{
		sql->bind(1, archive.filename());
		if (sql->executeStep())
			id = sql->getColumn(0).getInt();
		sql->reset();
	}

	return id;
}

sigslot::signal<>& library::signalUpdated()
{
	return signal_updated;
}