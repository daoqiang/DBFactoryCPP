#pragma once
#include <string>
#include <sstream>
#include <sqlite3.h>
#include <boost/archive/xml_oarchive.hpp> 
#include <boost/archive/xml_iarchive.hpp> 
#include <boost/serialization/vector.hpp>
#include <iostream> 
#include <fstream> 
#include "../Base/CSyncLock.h"
#include "Logger.h"
#include "../Base/tinyxml2.h"
#pragma comment(lib,"tinyxml2.lib")

#pragma comment(lib,"sqlite3.lib")
namespace Utils {
	class SqLiteConnection
	{
	public:
		SqLiteConnection(std::string& filename)
		{
			if (sqlite3_open(filename.c_str(), &conn) != SQLITE_OK)
			{
				const char* error = sqlite3_errmsg(conn);
				sqlite3_close(conn);
			}
		}
		~SqLiteConnection()
		{
			if (conn != nullptr)
			{
				sqlite3_close(conn);
			}
		}
		sqlite3 * conn;
	private:

	};

	class SqLiteDB
	{
	public:
		SqLiteDB(std::string filename)
		{
			_filename = filename;
		};

		//查询
		template<typename  T>
		std::vector<T> Query(std::string sql) throw()
		{
			//根据T类型初始化一个xml 字符串流
			T t;
			std::stringstream buffer;
			{
				boost::archive::xml_oarchive out_archive(buffer);
				out_archive & BOOST_SERIALIZATION_NVP(t);
			}
			//连接sqlite
			char* error;
			std::shared_ptr<SqLiteConnection > DBase(new SqLiteConnection(_filename));
			char** result;
			int row_num = 0;
			int col_num = 0;
			if (sqlite3_get_table(DBase->conn, sql.c_str(), &result, &row_num, &col_num, &error) != SQLITE_OK)
			{
				//ERRORLOG("SQL:{} operator error，exception：{}", sql, error);
				sqlite3_free_table(result);
				if (error != nullptr)
				{
					sqlite3_free((void*)error);
				}
			}
			std::vector<T> res(row_num);
			std::string body(buffer.str());
			tinyxml2::XMLDocument doc;
			doc.Parse(body.c_str());
			tinyxml2::XMLElement* elemt = doc.RootElement()->FirstChildElement("t");
			int col_index = col_num;
			tinyxml2::XMLPrinter printer;
			for (int i = 0; i < row_num; i++)
			{
				for (int j = 0; j < col_num; j++)
				{
					std::string ress(result[j]);
					std::string restext(result[col_index]);
					elemt->FirstChildElement(result[j])->SetText(result[col_index]);
					++col_index;
				}
				printer.ClearBuffer();
				doc.Print(&printer);
				buffer.str(printer.CStr());
				boost::archive::xml_iarchive in_archive(buffer);
				in_archive >> BOOST_SERIALIZATION_NVP(t);
				res[i] = t;
			}
			sqlite3_free_table(result);
			return res;
		};

		//单条INSERT
		bool Insert(std::string& sql)
		{
			CSyncLock lock(&sync);
			char* error;
			SqLiteConnection DBase(_filename);
			if (sqlite3_exec(DBase.conn, sql.c_str(), NULL, NULL, &error) != SQLITE_OK)
			{
				if (error != nullptr)
				{
					//log todo
					sqlite3_free(error);
				}
				return false;
			}
			return true;
		}
		template<typename T>
		//对象INSERT
		bool Insert(T& t)
		{
			CSyncLock lock(&sync);
			std::shared_ptr<SqLiteConnection> DBase(new SqLiteConnection(_filename));
			std::string sql;
			std::string tablename= typeid(t).name();
			tablename=tablename.substr(tablename.find_last_of("::") + 1, tablename.length());
			std::stringstream buffer;
			boost::archive::xml_oarchive out_archive(buffer,1);
			out_archive & BOOST_SERIALIZATION_NVP(t);
			std::string body(buffer.str());
			tinyxml2::XMLDocument doc;
			doc.Parse(body.c_str());
			char* error;
			char tmp[256];
			tinyxml2::XMLElement* elemt = doc.RootElement()->FirstChildElement();
			if (elemt != NULL)
			{
				std::vector<std::string>names;
				std::vector<std::string>values;
				std::string name = elemt->Name();
				std::string value = elemt->GetText();
				while ((elemt = elemt->NextSiblingElement()) != NULL)
				{
					names.push_back(name);
					values.push_back(value);
					name = elemt->Name();
					value = elemt->GetText();
				}
				std::string nametmp;
				std::string valuetmp;
				int pos = 0;
				for (int i = 0; i < names.size(); i++)
				{
					if (nametmp.empty())
					{
						nametmp = nametmp.append(names[i]);
						valuetmp = valuetmp.append(values[i]);
					}
					nametmp = nametmp.append(",").append(names[i]);
					valuetmp = valuetmp.append(",").append(values[i]);
				}
				sprintf_s(tmp,"INSERT INTO %s(%s) VALUES(%s)", tablename.c_str(), nametmp.c_str(), valuetmp.c_str());
				sql = std::string(tmp);
			}
			if (sqlite3_exec(DBase->conn, sql.c_str(), NULL,NULL, &error) != SQLITE_OK)
			{
				if (error != nullptr)
				{
					ErrorLog("SQL:{} operator error，exception：{}", sql, error);
					sqlite3_free((void*)error);
					return false;
				}
			}
			return true;
		}
		//批量INSERT(事务)
		bool InsertBatch(std::string &sql)
		{
			CSyncLock lock(&sync);
			char* error;
			SqLiteConnection DBase(_filename);
			if (sqlite3_exec(DBase.conn, "BEGIN TRANSACTION", NULL, NULL, &error) != SQLITE_OK)
			{
				if (error != nullptr)
				{
					//ERRORLOG("SQL:{} operator error，exception：{}", sql, error);
					sqlite3_free(error);
				}
				return false;
			}
			if (sqlite3_exec(DBase.conn, sql.c_str(), NULL, NULL, &error) != SQLITE_OK)
			{
				if (error != nullptr)
				{
					//ERRORLOG("SQL:{} operator error，exception：{}", sql, error);
					sqlite3_free(error);
					sqlite3_exec(DBase.conn, "ROLLBACK", NULL, NULL, &error);
				}
				return false;
			}
			if (sqlite3_exec(DBase.conn, "COMMIT", NULL, NULL, &error) != SQLITE_OK)
			{
				if (error != nullptr)
				{
					//ERRORLOG("SQL:{} operator error，exception：{}", sql, error);
					sqlite3_free(error);
				}
				return false;
			}
			return true;
		}

		//UPDATE
		bool Update(std::string& sql)
		{
			CSyncLock lock(&sync);
			char* error;
			SqLiteConnection DBase(_filename);
			if (sqlite3_exec(DBase.conn, sql.c_str(), NULL, NULL, &error) != SQLITE_OK)
			{
				if (error != nullptr)
				{
					sqlite3_exec(DBase.conn, "ROLLBACK", NULL, NULL, &error);
					sqlite3_free(error);
				}
				return false;
			}
			return true;
		}

		//DELETE
		bool Delete(std::string& sql)
		{
			CSyncLock lock(&sync);
			char* error;
			SqLiteConnection DBase(_filename);
			if (sqlite3_exec(DBase.conn, sql.c_str(), NULL, NULL, &error) != SQLITE_OK)
			{
				if (error != nullptr)
				{
					sqlite3_exec(DBase.conn, "ROLLBACK", NULL, NULL, &error);
					sqlite3_free(error);
				}
				return false;
			}
			return true;
		}

		//除SELECT之外的语句都可以执行
		bool Execute(std::string& sql)
		{
			CSyncLock lock(&sync);
			char* error;
			SqLiteConnection DBase(_filename);
			if (sqlite3_exec(DBase.conn, sql.c_str(), NULL, NULL, &error) != SQLITE_OK)
			{
				if (error != nullptr)
				{
					sqlite3_exec(DBase.conn, "ROLLBACK", NULL, NULL, &error);
					sqlite3_free(error);
				}
				return false;
			}
			return true;
		}
		~SqLiteDB()
		{
		};
	private:
		CSync sync;
		std::string _filename;
	};
}