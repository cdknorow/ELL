////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Project:  Embedded Machine Learning Library (EMLL)
//  File:     utilities.i (interfaces)
//  Authors:  Chuck Jacobs
//
////////////////////////////////////////////////////////////////////////////////////////////////////

%include "stl.i"

%ignore emll::utilities::operator<<;
%ignore emll::utilities::MakeAnyIterator;
%ignore emll::utilities::IteratorWrapper;

%{
#define SWIG_FILE_WITH_INIT
#include "AnyIterator.h"
#include "RandomEngines.h"
#include "StlIterator.h"
#include "ObjectArchive.h"
#include "IArchivable.h"
#include "Archiver.h"
#include "JsonArchiver.h"
#include "XmlArchiver.h"
#include "UniqueId.h"
#include "Variant.h"
%}

// ignores
%ignore std::enable_if_t<>;
%ignore std::hash<emll::utilities::UniqueId>;

%ignore emll::utilities::JsonUtilities;
%ignore emll::utilites::Variant::Variant(emll::utilities::Variant&&);
%ignore emll::utilities::CompressedIntegerList;

namespace emll { namespace utilities {} };

// SWIG can't interpret StlIterator.h, so we need to include a simpler signature of the class
template <typename IteratorType, typename ValueType>
class emll::utilities::StlIterator
{
public:
    StlIterator();
    StlIterator(IteratorType begin, IteratorType end);
    bool IsValid() const;
    bool HasSize() const;
    uint64_t NumIteratesLeft() const;
    void Next();
    const ValueType& Get() const;
};

// SWIG can't interpret StlIndexValueIterator.h, so we need to include a simpler signature of the class
template <typename IteratorType, typename ValueType>
class emll::utilities::StlIndexValueIterator
{
public:
    StlIndexValueIterator();
    StlIndexValueIterator(IteratorType begin, IteratorType end);
    bool IsValid() const;
    bool HasSize() const;
    uint64_t NumIteratesLeft() const;
    void Next();
    linear::IndexValue Get() const;
};


// utilities
%include "TypeFactory.h"
%include "CompressedIntegerList.h"
%include "Archiver.h"
%include "Variant.h"
%include "ObjectArchive.h"
%include "IArchivable.h"
%include "JsonArchiver.h"
%include "XmlArchiver.h"
%include "UniqueId.h"
%include "Variant.h"
%include "AnyIterator.h"
%include "RandomEngines.h"

// wrap print
WRAP_OSTREAM_OUT_TO_STR(emll::utilities::UniqueId)
