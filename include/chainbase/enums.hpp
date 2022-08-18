#pragma once

namespace chainbase {
	/**
	 *  Database open mode
	 */
	enum class open_mode {
		read_only,
		read_write,
		read_write_no_journal,
	};

	/**
	 *  Database open action to be applied if dirty flag is set
	 *
	 *  What action should be applied on open if dirty flag is set.
	 *   - fail:  fails with an error
	 *   - reset:
	 *   - allow: does nothing
	 */
	enum class dirty_action {
		fail,  /// fail with an error/exception
		allow, /// do nothing
		reset, /// reset the database to a clean state over the existing one
	};

	/**
	 *  Database open outcome
	 *
	 *  The state of database after opening
	 */
	enum class open_outcome {
		good,      /// database file was opened with no issues
		created,   /// a new database file was created
		corrupted, /// database file dirty flag was set, data might be corrupted, proceed at your own risk
		reset,     /// database file dirty flag was set, data has been reset to a clean state
	};
}
