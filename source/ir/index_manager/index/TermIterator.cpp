#include <ir/index_manager/index/TermIterator.h>
#include <ir/index_manager/index/TermInfo.h>
#include <ir/index_manager/index/InputDescriptor.h>
#include <ir/index_manager/index/Posting.h>
#include <ir/index_manager/index/TermReader.h>

#include <sstream>

using namespace std;

using namespace izenelib::ir::indexmanager;

TermIterator::TermIterator(void)
        :pBuffer_(NULL)
        ,nBuffSize_(0)
{
}

TermIterator::~TermIterator(void)
{
    pBuffer_ = NULL;
    nBuffSize_ = 0;
}
size_t TermIterator::setBuffer(char* pBuffer,size_t bufSize)
{
    this->pBuffer_ = pBuffer;
    nBuffSize_ = bufSize;
    return bufSize;
}

VocIterator::VocIterator(VocReader* termReader)
        :pTermReader_(termReader)
        ,pCurTerm_(NULL)
        ,pCurTermInfo_(NULL)
        ,pCurTermPosting_(NULL)
        ,pInputDescriptor_(NULL)
        ,nCurPos_(-1)
{
}

VocIterator::~VocIterator(void)
{
    if (pCurTerm_)
    {
        delete pCurTerm_;
        pCurTerm_ = NULL;
    }
    if (pCurTermPosting_)
    {
        delete pCurTermPosting_;
        pCurTermPosting_ = NULL;
    }
    if (pInputDescriptor_)
    {
        delete pInputDescriptor_;
        pInputDescriptor_ = NULL;
    }
    pCurTermInfo_ = NULL;
}

const Term* VocIterator::term()
{
    return pCurTerm_;
}

const TermInfo* VocIterator::termInfo()
{
    return pCurTermInfo_;
}

Posting* VocIterator::termPosting()
{
    if (!pCurTermPosting_)
    {
        IndexInput* pInput;
        pInputDescriptor_ = new InputDescriptor(true);
        pInput = pTermReader_->getTermReaderImpl()->pInputDescriptor_->getDPostingInput()->clone();
        pInputDescriptor_->setDPostingInput(pInput);
        pInput = pTermReader_->getTermReaderImpl()->pInputDescriptor_->getPPostingInput()->clone();
        pInputDescriptor_->setPPostingInput(pInput);
        pCurTermPosting_ = new OnDiskPosting(pInputDescriptor_,pCurTermInfo_->docPointer());
    }
    else
    {
        ((OnDiskPosting*)pCurTermPosting_)->reset(pCurTermInfo_->docPointer());///reset to a new posting
    }
    return pCurTermPosting_;
}

size_t VocIterator::setBuffer(char* pBuffer,size_t bufSize)
{
    int64_t nDLen,nPLen;
    pTermReader_->getFieldInfo()->getLength(NULL,&nDLen,&nPLen);
    if ( (size_t)(nDLen + nPLen) < bufSize)
    {
        TermIterator::setBuffer(pBuffer,(size_t)(nDLen + nPLen));
        return (size_t)(nDLen + nPLen);
    }
    else
    {
        TermIterator::setBuffer(pBuffer,bufSize);
        return bufSize;
    }
}

bool VocIterator::next()
{
    if(pTermReader_->getTermReaderImpl()->nTermCount_ > nCurPos_+1)			
    {
        if(pCurTerm_ == NULL)
            pCurTerm_ = new Term(pTermReader_->getFieldInfo()->getName(),pTermReader_->getTermReaderImpl()->pTermTable_[++nCurPos_].tid);
        else 
            pCurTerm_->setValue(pTermReader_->getTermReaderImpl()->pTermTable_[++nCurPos_].tid);
        pCurTermInfo_ = &(pTermReader_->getTermReaderImpl()->pTermTable_[nCurPos_].ti);
        return true;
    }
    else return false;
}

//////////////////////////////////////////////////////////////////////////
///DiskTermIterator
DiskTermIterator::DiskTermIterator(Directory* pDirectory,const char* barrelname,FieldInfo* pFieldInfo)
    :pDirectory_(pDirectory)
    ,pFieldInfo_(pFieldInfo)
    ,pCurTerm_(NULL)
    ,pCurTermInfo_(NULL)
    ,pCurTermPosting_(NULL)
    ,pInputDescriptor_(NULL)
    ,nCurPos_(-1)
{
    barrelName_ = barrelname;
    pVocInput_ = pDirectory->openInput(barrelName_ + ".voc");
    pVocInput_->seek(pFieldInfo->getIndexOffset());
    fileoffset_t voffset = pVocInput_->getFilePointer();
    ///begin read vocabulary descriptor
    nVocLength_ = pVocInput_->readLong();
    nTermCount_ = (int32_t)pVocInput_->readLong(); ///get total term count
    ///end read vocabulary descriptor
    pVocInput_->seek(voffset - nVocLength_);///seek to begin of vocabulary data

}

DiskTermIterator::~DiskTermIterator()
{
    delete pVocInput_;
    if (pCurTerm_)
    {
        delete pCurTerm_;
        pCurTerm_ = NULL;
    }
    if (pCurTermPosting_)
    {
        delete pCurTermPosting_;
        pCurTermPosting_ = NULL;
    }
    if (pInputDescriptor_)
    {
        delete pInputDescriptor_;
        pInputDescriptor_ = NULL;
    }
    if(pCurTermInfo_)
    {
        delete pCurTermInfo_;
        pCurTermInfo_ = NULL;
    }
}

const Term* DiskTermIterator::term()
{
    return pCurTerm_;
}

const TermInfo* DiskTermIterator::termInfo()
{
    return pCurTermInfo_;
}

Posting* DiskTermIterator::termPosting()
{
    if (!pCurTermPosting_)
    {
        pInputDescriptor_ = new InputDescriptor(true);
        pInputDescriptor_->setDPostingInput(pDirectory_->openInput(barrelName_ + ".dfp"));
        pInputDescriptor_->setPPostingInput(pDirectory_->openInput(barrelName_ + ".pop"));
        pCurTermPosting_ = new OnDiskPosting(pInputDescriptor_,pCurTermInfo_->docPointer());
    }
    else
    {
        ((OnDiskPosting*)pCurTermPosting_)->reset(pCurTermInfo_->docPointer());///reset to a new posting
    }
    return pCurTermPosting_;
}

bool DiskTermIterator::next()
{
    if(nTermCount_ > nCurPos_+1) 		
    {
        ++nCurPos_;
        termid_t tid = pVocInput_->readInt();
        freq_t df = pVocInput_->readInt();
        fileoffset_t dfiP = pVocInput_->readLong();

        if(pCurTerm_ == NULL)
            pCurTerm_ = new Term(pFieldInfo_->getName(),tid);
        else 
            pCurTerm_->setValue(tid);
        if(pCurTermInfo_ == NULL)
            pCurTermInfo_ = new TermInfo(df,dfiP);
        else
            pCurTermInfo_->set(df,dfiP);
        return true;
    }
    else return false;
}


//////////////////////////////////////////////////////////////////////////
//
InMemoryTermIterator::InMemoryTermIterator(InMemoryTermReader* pTermReader)
        :pTermReader_(pTermReader)
        ,pCurTerm_(NULL)
        ,pCurTermInfo_(NULL)
        ,pCurTermPosting_(NULL)
        ,postingIterator_(pTermReader->pIndexer_->postingMap_.begin())
        ,postingIteratorEnd_(pTermReader->pIndexer_->postingMap_.end())
{
}

InMemoryTermIterator::~InMemoryTermIterator(void)
{
    if (pCurTerm_)
    {
        delete pCurTerm_;
        pCurTerm_ = NULL;
    }
    pCurTermPosting_ = NULL;
    if (pCurTermInfo_)
    {
        delete pCurTermInfo_;
        pCurTermInfo_ = NULL;
    }
}

bool InMemoryTermIterator::next()
{
    postingIterator_++;

    if(postingIterator_ != postingIteratorEnd_)
    {
        pCurTermPosting_ = postingIterator_->second;
        while (pCurTermPosting_->hasNoChunk())
        {
            postingIterator_++;
            if(postingIterator_ != postingIteratorEnd_)
                pCurTermPosting_ = postingIterator_->second;
            else return false;
        }

        //TODO
        if (pCurTerm_ == NULL)
            pCurTerm_ = new Term(pTermReader_->field_.c_str(),postingIterator_->first);
        else pCurTerm_->setValue(postingIterator_->first);
        pCurTermPosting_ = postingIterator_->second;
        if (pCurTermInfo_ == NULL)
            pCurTermInfo_ = new TermInfo(pCurTermPosting_->docFreq(),-1);
        else
            pCurTermInfo_->set(pCurTermPosting_->docFreq(),-1);
        return true;
    }
    else return false;
}

const Term* InMemoryTermIterator::term()
{
    return pCurTerm_;
}
const TermInfo* InMemoryTermIterator::termInfo()
{
    return pCurTermInfo_;
}
Posting* InMemoryTermIterator::termPosting()
{
    return pCurTermPosting_;
}

size_t InMemoryTermIterator::setBuffer(char* pBuffer,size_t bufSize)
{
    return 0;///don't need buffer
}

