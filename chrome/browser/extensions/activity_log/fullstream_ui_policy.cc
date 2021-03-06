// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/activity_log/fullstream_ui_policy.h"

#include "base/callback.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/json/json_string_value_serializer.h"
#include "base/logging.h"
#include "base/strings/string16.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/extensions/activity_log/activity_action_constants.h"
#include "chrome/browser/extensions/activity_log/activity_database.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/dom_action_types.h"
#include "chrome/common/extensions/extension.h"
#include "sql/error_delegate_util.h"
#include "sql/transaction.h"
#include "url/gurl.h"

using base::Callback;
using base::FilePath;
using base::Time;
using base::Unretained;
using content::BrowserThread;

namespace constants = activity_log_constants;

namespace extensions {

const char* FullStreamUIPolicy::kTableName = "activitylog_full";
const char* FullStreamUIPolicy::kTableContentFields[] = {
  "extension_id", "time", "action_type", "api_name", "args", "page_url",
  "page_title", "arg_url", "other"
};
const char* FullStreamUIPolicy::kTableFieldTypes[] = {
  "LONGVARCHAR NOT NULL", "INTEGER", "INTEGER", "LONGVARCHAR", "LONGVARCHAR",
  "LONGVARCHAR", "LONGVARCHAR", "LONGVARCHAR", "LONGVARCHAR"
};
const int FullStreamUIPolicy::kTableFieldCount =
    arraysize(FullStreamUIPolicy::kTableContentFields);

FullStreamUIPolicy::FullStreamUIPolicy(Profile* profile)
    : ActivityLogDatabasePolicy(
          profile,
          FilePath(chrome::kExtensionActivityLogFilename)) {}

FullStreamUIPolicy::~FullStreamUIPolicy() {}

bool FullStreamUIPolicy::InitDatabase(sql::Connection* db) {
  if (!Util::DropObsoleteTables(db))
    return false;

  // Create the unified activity log entry table.
  return ActivityDatabase::InitializeTable(db,
                                           kTableName,
                                           kTableContentFields,
                                           kTableFieldTypes,
                                           arraysize(kTableContentFields));
}

bool FullStreamUIPolicy::FlushDatabase(sql::Connection* db) {
  if (queued_actions_.empty())
    return true;

  sql::Transaction transaction(db);
  if (!transaction.Begin())
    return false;

  std::string sql_str =
      "INSERT INTO " + std::string(FullStreamUIPolicy::kTableName) +
      " (extension_id, time, action_type, api_name, args, "
      "page_url, page_title, arg_url, other) VALUES (?,?,?,?,?,?,?,?,?)";
  sql::Statement statement(db->GetCachedStatement(
      sql::StatementID(SQL_FROM_HERE), sql_str.c_str()));

  Action::ActionVector::size_type i;
  for (i = 0; i != queued_actions_.size(); ++i) {
    const Action& action = *queued_actions_[i];
    statement.Reset(true);
    statement.BindString(0, action.extension_id());
    statement.BindInt64(1, action.time().ToInternalValue());
    statement.BindInt(2, static_cast<int>(action.action_type()));
    statement.BindString(3, action.api_name());
    if (action.args()) {
      statement.BindString(4, Util::Serialize(action.args()));
    }
    std::string page_url_string = action.SerializePageUrl();
    if (!page_url_string.empty()) {
      statement.BindString(5, page_url_string);
    }
    if (!action.page_title().empty()) {
      statement.BindString(6, action.page_title());
    }
    std::string arg_url_string = action.SerializeArgUrl();
    if (!arg_url_string.empty()) {
      statement.BindString(7, arg_url_string);
    }
    if (action.other()) {
      statement.BindString(8, Util::Serialize(action.other()));
    }

    if (!statement.Run()) {
      LOG(ERROR) << "Activity log database I/O failed: " << sql_str;
      return false;
    }
  }

  if (!transaction.Commit())
    return false;

  queued_actions_.clear();
  return true;
}

scoped_ptr<Action::ActionVector> FullStreamUIPolicy::DoReadFilteredData(
    const std::string& extension_id,
    const Action::ActionType type,
    const std::string& api_name,
    const std::string& page_url,
    const std::string& arg_url,
    const int days_ago) {
  // Ensure data is flushed to the database first so that we query over all
  // data.
  activity_database()->AdviseFlush(ActivityDatabase::kFlushImmediately);
  scoped_ptr<Action::ActionVector> actions(new Action::ActionVector());

  sql::Connection* db = GetDatabaseConnection();
  if (!db) {
    return actions.Pass();
  }

  // Build up the query based on which parameters were specified.
  std::string where_str = "";
  std::string where_next = "";
  if (!extension_id.empty()) {
    where_str += "extension_id=?";
    where_next = " AND ";
  }
  if (!api_name.empty()) {
    where_str += where_next + "api_name=?";
    where_next = " AND ";
  }
  if (type != Action::ACTION_ANY) {
    where_str += where_next + "action_type=?";
    where_next = " AND ";
  }
  if (!page_url.empty()) {
    where_str += where_next + "page_url LIKE ?";
    where_next = " AND ";
  }
  if (!arg_url.empty()) {
    where_str += where_next + "arg_url LIKE ?";
  }
  if (days_ago >= 0)
    where_str += where_next + "time BETWEEN ? AND ?";
  std::string query_str = base::StringPrintf(
      "SELECT extension_id,time,action_type,api_name,args,page_url,page_title,"
      "arg_url,other FROM %s %s %s ORDER BY time DESC LIMIT 300",
      kTableName,
      where_str.empty() ? "" : "WHERE",
      where_str.c_str());
  sql::Statement query(db->GetUniqueStatement(query_str.c_str()));
  int i = -1;
  if (!extension_id.empty())
    query.BindString(++i, extension_id);
  if (!api_name.empty())
    query.BindString(++i, api_name);
  if (type != Action::ACTION_ANY)
    query.BindInt(++i, static_cast<int>(type));
  if (!page_url.empty())
    query.BindString(++i, page_url + "%");
  if (!arg_url.empty())
    query.BindString(++i, arg_url + "%");
  if (days_ago >= 0) {
    int64 early_bound;
    int64 late_bound;
    Util::ComputeDatabaseTimeBounds(Now(), days_ago, &early_bound, &late_bound);
    query.BindInt64(++i, early_bound);
    query.BindInt64(++i, late_bound);
  }

  // Execute the query and get results.
  while (query.is_valid() && query.Step()) {
    scoped_refptr<Action> action =
        new Action(query.ColumnString(0),
                   base::Time::FromInternalValue(query.ColumnInt64(1)),
                   static_cast<Action::ActionType>(query.ColumnInt(2)),
                   query.ColumnString(3));

    if (query.ColumnType(4) != sql::COLUMN_TYPE_NULL) {
      scoped_ptr<Value> parsed_value(
          base::JSONReader::Read(query.ColumnString(4)));
      if (parsed_value && parsed_value->IsType(Value::TYPE_LIST)) {
        action->set_args(
            make_scoped_ptr(static_cast<ListValue*>(parsed_value.release())));
      }
    }

    action->ParsePageUrl(query.ColumnString(5));
    action->set_page_title(query.ColumnString(6));
    action->ParseArgUrl(query.ColumnString(7));

    if (query.ColumnType(8) != sql::COLUMN_TYPE_NULL) {
      scoped_ptr<Value> parsed_value(
          base::JSONReader::Read(query.ColumnString(8)));
      if (parsed_value && parsed_value->IsType(Value::TYPE_DICTIONARY)) {
        action->set_other(make_scoped_ptr(
            static_cast<DictionaryValue*>(parsed_value.release())));
      }
    }
    actions->push_back(action);
  }

  return actions.Pass();
}

void FullStreamUIPolicy::DoRemoveURLs(const std::vector<GURL>& restrict_urls) {
  sql::Connection* db = GetDatabaseConnection();
  if (!db) {
    LOG(ERROR) << "Unable to connect to database";
    return;
  }

  // Make sure any queued in memory are sent to the database before cleaning.
  activity_database()->AdviseFlush(ActivityDatabase::kFlushImmediately);

  // If no restrictions then then all URLs need to be removed.
  if (restrict_urls.empty()) {
    sql::Statement statement;
    std::string sql_str = base::StringPrintf(
        "UPDATE %s SET page_url=NULL,page_title=NULL,arg_url=NULL",
        kTableName);
    statement.Assign(db->GetCachedStatement(
        sql::StatementID(SQL_FROM_HERE), sql_str.c_str()));

    if (!statement.Run()) {
      LOG(ERROR) << "Removing URLs from database failed: "
                 << statement.GetSQLStatement();
    }
    return;
  }

  // If URLs are specified then restrict to only those URLs.
  for (size_t i = 0; i < restrict_urls.size(); ++i) {
    if (!restrict_urls[i].is_valid()) {
      continue;
    }

    // Remove any matching page url info.
    sql::Statement statement;
    std::string sql_str = base::StringPrintf(
      "UPDATE %s SET page_url=NULL,page_title=NULL WHERE page_url=?",
      kTableName);
    statement.Assign(db->GetCachedStatement(
        sql::StatementID(SQL_FROM_HERE), sql_str.c_str()));
    statement.BindString(0, restrict_urls[i].spec());

    if (!statement.Run()) {
      LOG(ERROR) << "Removing page URL from database failed: "
                 << statement.GetSQLStatement();
      return;
    }

    // Remove any matching arg urls.
    sql_str = base::StringPrintf("UPDATE %s SET arg_url=NULL WHERE arg_url=?",
                                 kTableName);
    statement.Assign(db->GetCachedStatement(
        sql::StatementID(SQL_FROM_HERE), sql_str.c_str()));
    statement.BindString(0, restrict_urls[i].spec());

    if (!statement.Run()) {
      LOG(ERROR) << "Removing arg URL from database failed: "
                 << statement.GetSQLStatement();
      return;
    }
  }
}

void FullStreamUIPolicy::DoRemoveExtensionData(
    const std::string& extension_id) {
  if (extension_id.empty())
    return;

  sql::Connection* db = GetDatabaseConnection();
  if (!db) {
    LOG(ERROR) << "Unable to connect to database";
    return;
  }

  // Make sure any queued in memory are sent to the database before cleaning.
  activity_database()->AdviseFlush(ActivityDatabase::kFlushImmediately);

  std::string sql_str = base::StringPrintf(
      "DELETE FROM %s WHERE extension_id=?", kTableName);
  sql::Statement statement;
  statement.Assign(
      db->GetCachedStatement(sql::StatementID(SQL_FROM_HERE), sql_str.c_str()));
  statement.BindString(0, extension_id);
  if (!statement.Run()) {
    LOG(ERROR) << "Removing URLs for extension "
               << extension_id << "from database failed: "
               << statement.GetSQLStatement();
  }
}

void FullStreamUIPolicy::DoDeleteDatabase() {
  sql::Connection* db = GetDatabaseConnection();
  if (!db) {
    LOG(ERROR) << "Unable to connect to database";
    return;
  }

  queued_actions_.clear();

  // Not wrapped in a transaction because the deletion should happen even if
  // the vacuuming fails.
  std::string sql_str = base::StringPrintf("DELETE FROM %s;", kTableName);
  sql::Statement statement(db->GetCachedStatement(
      sql::StatementID(SQL_FROM_HERE), sql_str.c_str()));
  if (!statement.Run()) {
    LOG(ERROR) << "Deleting the database failed: "
               << statement.GetSQLStatement();
    return;
  }
  statement.Clear();
  statement.Assign(db->GetCachedStatement(sql::StatementID(SQL_FROM_HERE),
                                          "VACUUM"));
  if (!statement.Run()) {
    LOG(ERROR) << "Vacuuming the database failed: "
               << statement.GetSQLStatement();
  }
}

void FullStreamUIPolicy::OnDatabaseFailure() {
  queued_actions_.clear();
}

void FullStreamUIPolicy::OnDatabaseClose() {
  delete this;
}

void FullStreamUIPolicy::Close() {
  // The policy object should have never been created if there's no DB thread.
  DCHECK(BrowserThread::IsMessageLoopValid(BrowserThread::DB));
  ScheduleAndForget(activity_database(), &ActivityDatabase::Close);
}

void FullStreamUIPolicy::ReadFilteredData(
    const std::string& extension_id,
    const Action::ActionType type,
    const std::string& api_name,
    const std::string& page_url,
    const std::string& arg_url,
    const int days_ago,
    const base::Callback
        <void(scoped_ptr<Action::ActionVector>)>& callback) {
  BrowserThread::PostTaskAndReplyWithResult(
      BrowserThread::DB,
      FROM_HERE,
      base::Bind(&FullStreamUIPolicy::DoReadFilteredData,
                 base::Unretained(this),
                 extension_id,
                 type,
                 api_name,
                 page_url,
                 arg_url,
                 days_ago),
      callback);
}

void FullStreamUIPolicy::RemoveURLs(const std::vector<GURL>& restrict_urls) {
  ScheduleAndForget(this, &FullStreamUIPolicy::DoRemoveURLs, restrict_urls);
}

void FullStreamUIPolicy::RemoveExtensionData(const std::string& extension_id) {
  ScheduleAndForget(
      this, &FullStreamUIPolicy::DoRemoveExtensionData, extension_id);
}

void FullStreamUIPolicy::DeleteDatabase() {
  ScheduleAndForget(this, &FullStreamUIPolicy::DoDeleteDatabase);
}

scoped_refptr<Action> FullStreamUIPolicy::ProcessArguments(
    scoped_refptr<Action> action) const {
  return action;
}

void FullStreamUIPolicy::ProcessAction(scoped_refptr<Action> action) {
  // TODO(mvrable): Right now this argument stripping updates the Action object
  // in place, which isn't good if there are other users of the object.  When
  // database writing is moved to policy class, the modifications should be
  // made locally.
  action = ProcessArguments(action);
  ScheduleAndForget(this, &FullStreamUIPolicy::QueueAction, action);
}

void FullStreamUIPolicy::QueueAction(scoped_refptr<Action> action) {
  if (activity_database()->is_db_valid()) {
    queued_actions_.push_back(action);
    activity_database()->AdviseFlush(queued_actions_.size());
  }
}

}  // namespace extensions
