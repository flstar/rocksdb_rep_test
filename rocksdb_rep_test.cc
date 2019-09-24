#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include <thread>
#include <string>
#include <mutex>

#include "rocksdb/db.h"
#include "rocksdb/utilities/checkpoint.h"

rocksdb::DB * init_db(const char *path)
{
	rocksdb::Options options;
	options.create_if_missing = true;
	rocksdb::DB *db = nullptr;
	
	rocksdb::Status status = rocksdb::DB::Open(options, path, &db);
	assert(status.ok());
	
	return db;
}

void uninit_db(rocksdb::DB *db)
{
	delete db;
}

rocksdb::DB *db = nullptr;
rocksdb::DB *db2 = nullptr;
std::atomic<bool> stop;

std::mutex syncedMutex;
bool synced;

bool insert_data(int x)
{
	char key[32];
	sprintf(key, "%010d", x);
	
	std::unique_lock<std::mutex> _lock(syncedMutex);
	printf("Insert key %s to primary db\n", key);
	rocksdb::Status status = db->Put(rocksdb::WriteOptions(), key, "");
	if (status.ok() && synced) {
		printf("Insert key %s to secondary db\n", key);
		rocksdb::Status status2 = db2->Put(rocksdb::WriteOptions(), key, "");
		if (!status2.ok()) {
			synced = false;
		}
	}

	return status.ok();
}

void work_thread_entry()
{
	for (int i=1; !stop ;i++) {
		bool ret = insert_data(i);
		assert(ret);
		usleep(1);
	}
}

rocksdb::Status copy_rocksdb(const char *path)
{
	std::string cmd = std::string("rm -rf ") + path;
	system(cmd.c_str());
	
	rocksdb::Checkpoint *ckpt = nullptr;
	rocksdb::Status status = rocksdb::Checkpoint::Create(db, &ckpt);
	if (!status.ok()) {
		return status;
	}

	status = ckpt->CreateCheckpoint(path);
	return status;
}

rocksdb::Status catch_up()
{
	rocksdb::SequenceNumber seq = db2->GetLatestSequenceNumber();
	std::unique_ptr<rocksdb::TransactionLogIterator> iter;
	rocksdb::Status status = db->GetUpdatesSince(seq + 1, &iter);
	if (!status.ok()) {
		return status;
	}

	while (iter->Valid()) {
		rocksdb::BatchResult br = iter->GetBatch();
		if (br.sequence != (++seq)) {
			return rocksdb::Status::Expired();
		}
		printf("Catch up at %ld\n", br.sequence);
		status = db2->Write(rocksdb::WriteOptions(), br.writeBatchPtr.get());
		if (!status.ok()) {
			return status;
		}
		iter->Next();
	}

	return rocksdb::Status::OK();
}

void sync_db2(const std::string &path2)
{
	system(("rm -rf " + path2).c_str());
	rocksdb::Status status = copy_rocksdb(path2.c_str());
	assert(status.ok());
	
	db2 = init_db(path2.c_str());
	printf("Checkpoint ends at %ld\n", db2->GetLatestSequenceNumber());
	
	status = catch_up();
	assert(status.ok() || status.IsNotFound());
	printf("Catch up to %ld\n", db2->GetLatestSequenceNumber());
	usleep(10);		// Delay for work thread to insert some additional records
					// to show the work of 2nd round of catch_up
	{
		std::unique_lock<std::mutex> _lock(syncedMutex);
		printf("Pause work thread and catch up again\n");

		status = catch_up();
		assert(status.ok() || status.IsNotFound());
		
		printf("Set synced flag and resume work thread\n");
		synced = true;
	}
}

int main()
{
	std::string dbpath("db"), dbpath2("db2");
	
	system(("rm -rf " + dbpath).c_str());
	db = init_db(dbpath.c_str());
	
	stop = false;
	synced = false;
	std::thread th(work_thread_entry);
	
	usleep(100*1000);
	
	sync_db2(dbpath2);
	
	usleep(10*1000);
	
	stop = true;
	th.join();
	
	uninit_db(db2);
	uninit_db(db);
}

