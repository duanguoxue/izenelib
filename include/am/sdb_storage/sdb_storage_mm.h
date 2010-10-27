/**
 * @file sdb_storage.h
 * @brief The header file of sdb_hash.
 * @author peisheng wang 
 * 
 * @history
 * ==========================
 * 1. 2009-08-06 first version.
 * 
 *
 * This file defines class sdb_hash.
 */
#ifndef SDB_STORAGE_MM_H
#define SDB_STORAGE_MM_H

#include <string>
#include <iostream>
#include <types.h>
#include <sys/stat.h>
#include <stdio.h>

//#include "sdb_storage_header.h"
#include "sdb_storage_types.h"

#include <am/sdb_btree/sdb_btree.h>
#include <am/sdb_hash/sdb_fixedhash.h>
#include <util/ThreadModel.h>
#include <cache/IzeneCache.h>
#include <3rdparty/compression/minilzo/minilzo.h>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem.hpp>
#include <boost/shared_array.hpp>

NS_IZENELIB_AM_BEGIN

class memory_map {
private:
	std::string path_; // path to the current file
	size_t offSet_; // still left in the file
	size_t mapSize_;
	boost::iostreams::mapped_file m_file_; // current memory map source   
public:
	memory_map(const std::string& path, long offSet=0, size_t mapSize=1024*1024*1024):
	path_(path),offSet_(0),mapSize_(mapSize),
	m_file_(path, ios_base::in | ios_base::out, mapSize, offSet) {
	}

	~memory_map(){
		m_file_.close();
	}

	size_t read (long offSet, void * buf, size_t num_bytes) {
		if(offSet + num_bytes> mapSize_){			
		  return size_t(-1);
		}
		memcpy(buf, m_file_.data()+offSet, num_bytes);
		return num_bytes;
	}

	size_t write(void * buf, size_t num_bytes) {
		if(offSet_+num_bytes < mapSize_) {
			memcpy(m_file_.data()+offSet_, buf, num_bytes);
			offSet_ += num_bytes;
			return num_bytes;
		}
		assert(offSet_+num_bytes <= mapSize_  );
		return size_t(-1);
	}

	void setOffset(size_t off) {
		offSet_ = off;
	}

	size_t getOffset() {
		return offSet_;
	}

	void* header() {
		return m_file_.data();
	}

	void display() {
		cout<<"memory_map info:"<<endl;
		cout<<"path_:" <<path_<<endl;
		cout<<"offset_: "<<offSet_<<endl;
	}
};

struct mm_header {
	int idx;
	size_t mapSize;
	long offset;
	size_t tsz;
	size_t maxdatasize;

	mm_header() :
	idx(0), offset(sizeof(mm_header)), tsz(sizeof(mm_header)), maxdatasize(0) {
		mapSize =128*1024*1024;
	}
	void read(void* fileBuffer) {
		memcpy(this, fileBuffer, sizeof(mm_header));
	}
	void write(void* fileBuffer) {
		memcpy(fileBuffer, this, sizeof(mm_header));
	}

	void display(std::ostream& os = std::cout) {
		cout<<"Display sdb_store_mm value: "<<endl;
		cout<<"idx: "<<idx<<endl;
		cout<<"mapSize: "<<mapSize<<endl;
		cout<<"offset: "<<offset<<endl;
		cout<<"tsz: "<<tsz<<endl;
		cout<<"maxdatasize: "<<maxdatasize<<endl;
	}
};

/**
 *  \brief  file version of array hash using Cache-Conscious Collision Resolution.
 * 
 *  sdb_hash is built on array hash using Cache-Conscious Collision Resolution.
 * 
 *  For file version, there is a little different, each bucket is now a bucket_chain, and each bucket_chain
 *  hash fixed size.
 *   
 * 
 */
static lzo_align_t __LZO_MMODEL
wrkmem [((LZO1X_1_MEM_COMPRESS<<4)+(sizeof(lzo_align_t)-1))/sizeof(lzo_align_t)];

template< typename KeyType, typename ValueType, typename LockType =NullLock,
typename AmType=sdb_hash<KeyType, long, LockType>,
bool UseCompress = true > class sdb_storage_mm :
public AccessMethod<KeyType, ValueType, LockType> {
public:
	typedef AmType KeyHash;
	typedef typename AmType::SDBCursor SDBCursor;
public:
	/**
	 *   constructor
	 */
	sdb_storage_mm(const string& fileName = "sdb_storage") :
	ssh_(), fileName_(fileName+"_value.dat"),
	keyHash_(fileName+ "_key.dat") {
		//dataFile_ = 0;
		isOpen_ = false;
	}

	/**
	 *   deconstructor, close() will also be called here.
	 */
	virtual ~sdb_storage_mm() {
		//if (dataFile_)
		close();
	}

	void setMapSize(size_t mapSize) {
		assert(isOpen_ == false);
		ssh_.mapSize = mapSize;

	}

	/**
	 *  set directory size if fileHeader
	 * 
	 *  if not called use default size 4096
	 */
	void setDegree(size_t dpow) {
		assert(isOpen_ == false);
		keyHash_.setDegree(dpow);
	}

	/**
	 *  set cache size, if not called use default size 100000
	 */
	void setCacheSize(size_t cacheSize) {
		binCache_.setCacheSize(cacheSize);
		//ssh_.cacheSize = cacheSize;
	}

	/**
	 * 	\brief return the file name of the SequentialDB
	 */
	std::string getFileName() const {
		return fileName_;
	}

	/**
	 *  insert an item of DataType 
	 */
	bool insert(const DataType<KeyType,ValueType> & dat) {
		return insert(dat.get_key(), dat.get_value() );
	}

	/**
	 *  insert an item in key/value pair
	 */
	bool insert(const KeyType& key, const ValueType& val) {
		SDBCursor locn;
		if (keyHash_.search(key, locn))
		return false;
		else {
			keyHash_.insert(key, ssh_.tsz);
			return appendValue_(key, val);
		}
		return true;

	}

	/**
	 *  find an item, return pointer to the value.
	 *  Note that, there will be memory leak if not delete the value 
	 */
	ValueType* find(const KeyType & key) {
		long npos;
		if ( !keyHash_.get(key, npos) )
		return NULL;
		else {
			ValueType *pv = new ValueType;
			if (readValue_(npos, *pv) )
			return pv;
			else {
				delete pv;
				pv = 0;
				return NULL;
			}
		}

	}

	bool get(const KeyType& key, ValueType& val) {
		long npos;
		
		if ( !keyHash_.get(key, npos) )
		return false;
		else {
			return readValue_(npos, val);
		}

	}

	/**
	 *  delete  an item
	 */
	bool del(const KeyType& key) {
		return keyHash_.del(key);
	}

	/**
	 *  update  an item through DataType data
	 */
	bool update(const DataType<KeyType,ValueType> & dat) {
		return update(dat.get_key(), dat.get_value() );
	}

	/**
	 *  update  an item by key/value pair
	 */
	bool update(const KeyType& key, const ValueType& val) {
		long npos;
		if ( !keyHash_.get(key, npos) )
		return insert(key, val);
		else {
			keyHash_.update(key, ssh_.tsz);
			appendValue_(key, val);
		}
		return true;
	}

	/**
	 *  search an item
	 * 
	 *   @return SDBCursor
	 */
	SDBCursor search(const KeyType& key) {
		return keyHash_.search(key);
	}

	/**
	 *    another search function, flushCache_() will be called at the beginning,
	 * 
	 */
	bool search(const KeyType &key, SDBCursor &locn) {
		return keyHash_.search(key, locn);
	}

	/**
	 *  get the SDBCursor of first item in the first not empty bucket.
	 */
	SDBCursor get_first_locn() {
		return keyHash_.get_first_locn();
	}

	SDBCursor get_last_locn() {
		//	return keyHash_.get_last_locn();
	}

	bool get(const SDBCursor& locn, KeyType& key, ValueType& value) {
		long npos;
		bool ret =keyHash_.get(locn, key, npos);
		if (ret) {
			readValue_(npos, value);
		}
		return ret;
	}
	/**
	 *  get an item from given SDBCursor
	 */
	bool get(const SDBCursor& locn, DataType<KeyType,ValueType> & rec) {
		return get(locn, rec.key, rec.value);
	}

	/**
	 *   \brief sequential access method
	 * 
	 *   @param locn is the current SDBCursor, and will replaced next SDBCursor when route finished. 
	 *   @param rec is the item in SDBCursor locn.
	 *   @param sdir is sequential access direction, for hash is unordered, we only implement forward case.
	 *   
	 */
	bool seq(SDBCursor& locn, ESeqDirection sdir=ESD_FORWARD) {
		return keyHash_.seq(locn, sdir);
	}

	bool seq(SDBCursor& locn, KeyType& key, ESeqDirection sdir=ESD_FORWARD) {
		long npos;
		return keyHash_.seq(locn, key, npos, sdir);
	}

	bool seq(SDBCursor& locn, KeyType& key, ValueType& value,
			ESeqDirection sdir=ESD_FORWARD) {
		long npos;
		if (keyHash_.seq(locn, key, npos, sdir) )
		return readValue_(npos, value);
		else
		return false;
	}

	bool seq(SDBCursor& locn, DataType<KeyType,ValueType> & rec,
			ESeqDirection sdir=ESD_FORWARD) {
		return seq(locn, rec.key, rec.value, sdir);

	}

	/**
	 *   get the num of items 
	 */
	int num_items() {
		return keyHash_.num_items();
	}

	void optimize() {

	}

public:
	bool is_open() {
		return isOpen_;
	}
	/**
	 *   db must be opened to be used.
	 */
	bool open() {
		if (isOpen_)
		return true;

		keyHash_.open();
		if (boost::filesystem::exists(fileName_) ) {
			DLOG(INFO)<<"open exist "<<fileName_<<" ...\n"<<endl;
			mms_.reset(new memory_map(fileName_, 0, ssh_.mapSize));
			ssh_.read(mms_->header() );
			if (ssh_.idx > 0)
			mms_.reset(new memory_map(fileName_, 0, ssh_.mapSize*(ssh_.idx+1) ));
			mms_->setOffset(ssh_.offset);
		} else {
			DLOG(INFO)<<"creat new "<<fileName_<<" ...\n"<<endl;
			expandFile_(ssh_.idx);
			mms_.reset(new memory_map(fileName_, 0, ssh_.mapSize));
			mms_->setOffset(ssh_.offset);
			syncHeader_();
		}
		assert(keyHash_.is_open() == true);
		keyHash_.fillCache();

#ifdef DEBUG 
		ssh_.display();
#endif

		if( ssh_.maxdatasize > 0){
		workmem1_.reset(new unsigned char[ssh_.maxdatasize*2] );
		workmem2_.reset(new unsigned char[ssh_.maxdatasize] );
		}
		isOpen_ = true;
		return true;
	}
	/**
	 *   db should be closed after open, and  it will automatically called in deconstuctor.
	 */
	bool close() {
		if ( !isOpen_)
		return false;
		flush();
		isOpen_ = false;
		return true;
	}
	/**
	 *  write the dirty buckets to disk, not release the memory
	 *  
	 */
	void commit() {
		keyHash_.commit();
	}
	/**
	 *   Write the dirty buckets to disk and also free up most of the memory.
	 *   Note that, for efficieny, entry_[] is not freed up.
	 */
	void flush() {
		keyHash_.flush();

	}
	/**
	 *  display the info of sdb_storage
	 */
	void display(std::ostream& os = std::cout, bool onlyheader = true) {
		ssh_.display(os);
		cout<<"Display sdb_store_mm key: "<<endl;
		keyHash_.display(os, onlyheader);
	}

private:
	///SsHeader ssh_;
	mm_header ssh_;
	string fileName_;
	KeyHash keyHash_;
	bool isOpen_;

private:
	LockType fileLock_;
	izenelib::cache::ILRUCache<unsigned int, DbObjPtr, ReadWriteLock> binCache_;
	boost::shared_ptr<memory_map> mms_;
	boost::shared_array<unsigned char> workmem1_;
	boost::shared_array<unsigned char> workmem2_;
private:
	void syncHeader_() {
		memcpy(mms_->header(), &ssh_, sizeof(ssh_));
	}

	void expandFile_(int seq) {
		struct stat statbuf;
		bool creating = stat(fileName_.c_str(), &statbuf);
		FILE *fp = fopen(fileName_.c_str(), creating ? "w+b" : "r+b");
		fseek(fp, (seq+1)*ssh_.mapSize, SEEK_SET);
		putw(0, fp);
		fclose(fp);
	}

	/**
	 *   Allocate an bucket_chain element 
	 */
	inline bool appendValue_(const KeyType& key, const ValueType& val) {
		char* ptr;
		size_t vsize;

		izenelib::util::ScopedWriteLock<LockType> lock(fileLock_);

		izene_serialization<ValueType> izs(val);
		izs.write_image(ptr, vsize);

		if (UseCompress) {		
			if( vsize > ssh_.maxdatasize ) {
				ssh_.maxdatasize = vsize;
				workmem1_.reset(new unsigned char[ssh_.maxdatasize*2] );
				workmem2_.reset(new unsigned char[ssh_.maxdatasize] );
			}
			lzo_uint tmpTarLen;		
			int ret = lzo1x_1_compress((unsigned char*)ptr, vsize, workmem1_.get(), &tmpTarLen, wrkmem);
			assert(tmpTarLen < ssh_.maxdatasize*2);
			if ( ret != LZO_E_OK )
			return false;	
			ptr = (char*)workmem1_.get();			
			vsize = tmpTarLen;
		}

		if ( (size_t)ssh_.offset+2*sizeof(size_t)+vsize >= ssh_.mapSize*(ssh_.idx
						+1)) {						
			++ssh_.idx;
			expandFile_(ssh_.idx);
			mms_.reset(new memory_map(fileName_, 0, ssh_.mapSize*(ssh_.idx+1) ));
			mms_->setOffset(ssh_.offset);
		}

		{
			mms_->write(&vsize, sizeof(size_t));		
			mms_->write(ptr, vsize);
			ssh_.tsz += sizeof(size_t) + vsize;
			ssh_.offset += sizeof(size_t) + vsize;
		}
		syncHeader_();
		return true;
	}

	inline bool readValue_(long npos, ValueType& val) {

		DbObjPtr dp;
		char* ptr;
		size_t vsize = 0;
		bool cached = false;

		izenelib::util::ScopedWriteLock<LockType> lock(fileLock_);
		
		if ( binCache_.getValue(npos, dp) )
		{
			vsize = dp->getSize();
			ptr = (char*)dp->getData();
			cached = true;
		} else {		  
			mms_->read(npos, &vsize, sizeof(size_t));			
			assert(vsize> 0);
			ptr = new char[vsize];
			mms_->read(npos+sizeof(size_t), ptr, vsize);
			dp.reset(new DbObj(ptr, vsize));
			binCache_.insertValue(npos, dp);
			delete ptr;			
		}
		
		if (UseCompress) {
			//char *p =(char*)_tc_bzdecompress(ptr, vsize, &vsz);	
			lzo_uint tmpTarLen;
			int ret = lzo1x_decompress( (const unsigned char*) dp->getData(), vsize, workmem2_.get(), &tmpTarLen, NULL);
			assert(tmpTarLen <= ssh_.maxdatasize );
			if ( ret != LZO_E_OK )
			return false;
			vsize = tmpTarLen;			
			ptr = (char*)workmem2_.get();
		}

		izene_deserialization<ValueType> izd( (char*)ptr, vsize);
		izd.read_image(val);
		
		return true;
	}

};

NS_IZENELIB_AM_END

#endif
