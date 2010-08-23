/**-
 * @file	IDGenerator.h
 * @brief	Contain two types of IDGenerator, HashID and UniqueID.
 *          HashID use hash to genreate ID,
 *          UniqueID generate ID using a sequential number.
 * @author Wei Cao
 * @date 2009-08-07
 */

#ifndef _ID_GENERATOR_H_
#define _ID_GENERATOR_H_

#include <types.h>

#include <util/hashFunction.h>
#include <util/ThreadModel.h>
#include <sdb/SequentialDB.h>

#include "IDFactoryException.h"
#include "IDFactoryErrorString.h"

#include "NameIDTraits.h"


NS_IZENELIB_IR_BEGIN

namespace idmanager {

template <typename  NameString,
          typename  NameID>
class EmptyIDGenerator
{
public:

	/**
	 * @brief Constructor.
	 */
	EmptyIDGenerator(const string&){}

	/**
	 * @brief Always return false, which means failure to generate ID
	 */
	inline bool conv(const typename NameString::CharT* nameStringBuffer, const size_t nameStringLength, NameID& nameID)
	{
        return false;
	}

	/**
	 * @brief Always return false, which means failure to generate ID
	 */
	inline bool conv(const NameString& nameString, NameID& nameID)
	{
        return false;
	}

	void flush(){}

	void close(){}

	void display(){}

}; // end - template SequentialIDFactory



template <typename  NameString,
          typename  NameID>
class HashIDGenerator
{
public:

	/**
	 * @brief Constructor.
	 */
	HashIDGenerator(const string&){}

	/**
	 * @brief Convert String to ID, ID may be not unique
	 * @param nameStringBuffer the content of name string
	 * @param nameStringLength the length of name string
	 * @param nameID the NameID that may be not unique
	 * @return always false
	 */
	inline bool conv(const typename NameString::CharT* nameStringBuffer, const size_t nameStringLength, NameID& nameID)
	{
        nameID = NameIDTraits<NameID>::template hash<NameString>(nameStringBuffer, nameStringLength);
        return false;
	}


	/**
	 * @brief Convert String to ID, ID may be not unique
	 * @param nameString the name string
	 * @param nameID the NameID that may be not unique
	 * @return always false
	 */
	inline bool conv(const NameString& nameString, NameID& nameID)
	{
        nameID = NameIDTraits<NameID>::hash(nameString);
        return false;
	}

	void flush(){}

	void close(){}

	void display(){}

}; // end - template SequentialIDFactory


template <
          typename  NameString,
          typename  NameID,
          typename  LockType    = izenelib::util::NullLock,
          NameID    MinIDValue  = NameIDTraits<NameID>::MinValue,
          NameID    MaxIDValue  = NameIDTraits<NameID>::MaxValue>
class UniqueIDGenerator
{
	typedef izenelib::sdb::ordered_sdb<NameString, NameID, LockType> IdFinder;

public:

	/**
	 * @brief Constructor.
	 *
	 * @param sdbName       name of sdb storage.
	 */
	UniqueIDGenerator(const string& sdbName);

	virtual ~UniqueIDGenerator();

	/**
	 * @brief This function returns a unique name id given a name string.
	 * @param nameString the name string
	 * @param nameID the unique NameID
	 * @param insert whether insert nameString if it does not exist
	 * @return true if DocID already in dictionary
	 * @return false otherwise
	 */
	inline bool conv(const NameString& nameString, NameID& nameID,bool insert = true);

	/**
	 * @brief Convert String to ID, ID may be not unique
	 * @param nameStringBuffer the content of name string
	 * @param nameStringLength the length of name string
	 * @param nameID the NameID that may be not unique
	 * @return always false
	 */
	inline bool conv(const typename NameString::CharT* nameStringBuffer, const size_t nameStringLength, NameID& nameID)
	{
	    return conv(NameString(nameStringBuffer, nameStringLength), nameID);
	}

	/**
	 * @brief This function returns a unique name id given a name string, update old id to
	 * satisfy the incremental semantic
	 * @param nameString the name string
	 * @param oldID the old unique NameID
	 * @param updatedID the updated unique NameID
	 * @return true if DocID already in dictionary
	 * @return false otherwise
	 */
	inline bool conv(const NameString& nameString, NameID& oldID, NameID& updatedID);

	void flush()
	{
	    idFinder_.flush();
	}

	void close()
	{
	    idFinder_.close();
	}

	void display()
	{
		idFinder_.display();
	}

protected:

	NameID minID_; ///< An minimum ID.
	NameID maxID_; ///< An maximum ID.
	NameID newID_; ///< An ID for new name.
	string sdbName_;

	LockType mutex_;

	IdFinder idFinder_; ///< an indexer which gives ids according to the name.
}; // end - template SequentialIDFactory

template <typename NameString, typename NameID,
    typename LockType, NameID MinValueID, NameID MaxValueID>
UniqueIDGenerator<NameString, NameID,
    LockType, MinValueID, MaxValueID>::UniqueIDGenerator(
        const string& sdbName)
:
	minID_(MinValueID),
    maxID_(MaxValueID),
    newID_(MinValueID),
    sdbName_(sdbName),
    idFinder_(sdbName_ + "_name.sdb")
{
	idFinder_.open();

    // reset newID_
	if(idFinder_.numItems() > 0)
	{
	    NameID maxValue = MinValueID;
        NameString k; NameID v;
        typename IdFinder::SDBCursor locn = idFinder_.get_first_locn();
        while (idFinder_.get(locn, k, v) ) {
            if(maxValue < v)
                maxValue = v;
            idFinder_.seq(locn);
        }
        newID_ = maxValue + 1;
	}
} // end - SequentialIDFactory()

template <typename NameString, typename NameID,
    typename LockType, NameID MinValueID, NameID MaxValueID>
UniqueIDGenerator<NameString, NameID,
    LockType, MinValueID, MaxValueID>::~UniqueIDGenerator()
{
} // end - ~SequentialIDFactory()

template <typename NameString, typename NameID,
    typename LockType, NameID MinValueID, NameID MaxValueID>
inline bool UniqueIDGenerator<NameString, NameID,
    LockType, MinValueID, MaxValueID>::conv(
        const NameString& nameString,
        NameID& nameID,
        bool insert)
{
    mutex_.acquire_write_lock();

	// If name string is found, return the id.
	if (idFinder_.getValue(nameString, nameID) ) {
	    mutex_.release_write_lock();
		return true;
	} // end - if

       if(!insert)
       	{
       	    mutex_.release_write_lock();
           return false;
       	}

	// Because there's no name string in idFinder, create new id according to the string.
	nameID = newID_;
	newID_++;

	// check correctness of input nameID
	if (newID_> maxID_)
	{
	    mutex_.release_write_lock();
		throw IDFactoryException(SF1_ID_FACTORY_OUT_OF_BOUND, __LINE__, __FILE__);
	}

	idFinder_.insertValue(nameString, nameID);
    mutex_.release_write_lock();
	return false;
} // end - getNameIDByNameString()

template <typename NameString, typename NameID,
    typename LockType, NameID MinValueID, NameID MaxValueID>
inline bool UniqueIDGenerator<NameString, NameID,
    LockType, MinValueID, MaxValueID>::conv(
        const NameString& nameString,
        NameID& oldID,
        NameID& updatedID)
{
    mutex_.acquire_write_lock();

	// If name string is found, return the id.
	bool ret = idFinder_.getValue(nameString, oldID);

       if(!ret)
       	{
	   	oldID = 0;
		///will be removed until MIA can support index unexist documents from Update SCDs
		mutex_.release_write_lock();
		return ret;
       	}

	// Because there's no name string in idFinder, create new id according to the string.
	updatedID = newID_;
	newID_++;

	// check correctness of input nameID
	if (newID_> maxID_)
	{
	    mutex_.release_write_lock();
		throw IDFactoryException(SF1_ID_FACTORY_OUT_OF_BOUND, __LINE__, __FILE__);
	}

	idFinder_.update(nameString, updatedID);
    mutex_.release_write_lock();
	return ret;
} // end - updateNameIDByNameString()


}
// end - namespace idmanager

NS_IZENELIB_IR_END

#endif // #ifndef _HASH_ID_H_

