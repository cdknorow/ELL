////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Project:  Embedded Learning Library (ELL)
//  File:     ComputeContext.cpp (value)
//  Authors:  Kern Handa
//
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ComputeContext.h"
#include "Value.h"
#include "ValueScalar.h"

#include <utilities/include/TypeTraits.h>

#include <cassert>
#include <string>

namespace ell
{
namespace value
{

    using namespace detail;
    using namespace utilities;

    struct ComputeContext::FunctionScope
    {
        FunctionScope(ComputeContext& context, std::string fnName) :
            context(context)
        {
            context._stack.push({ fnName, {} });
        }

        ~FunctionScope() { context._stack.pop(); }

        ComputeContext& context;
    };

    namespace
    {

        // TODO: Make this the basis of an iterator for MemoryLayout
        bool IncrementMemoryCoordinateImpl(int dimension, std::vector<int>& coordinate, const std::vector<int>& maxCoordinate)
        {
            // base case
            if (dimension < 0)
            {
                return false;
            }

            if (++coordinate[dimension] >= maxCoordinate[dimension])
            {
                coordinate[dimension] = 0;
                return IncrementMemoryCoordinateImpl(dimension - 1, coordinate, maxCoordinate);
            }

            return true;
        }

        bool IncrementMemoryCoordinate(std::vector<int>& coordinate, const std::vector<int>& maxCoordinate)
        {
            assert(coordinate.size() == maxCoordinate.size());
            return IncrementMemoryCoordinateImpl(static_cast<int>(maxCoordinate.size()) - 1, coordinate, maxCoordinate);
        }

        Value ConstantDataToValue(ConstantData& data, std::optional<MemoryLayout> layout = {})
        {
            return std::visit(
                [&layout](auto&& vector) -> Value {
                    return Value(vector.data(), layout.value_or(MemoryLayout({ (int)vector.size() })));
                },
                data);
        }

        template <typename T>
        auto AllocateConstantDataImpl(size_t size)
        {
            return std::vector<T>(size);
        }

        ConstantData AllocateConstantData(ValueType type, size_t size)
        {
            switch (type)
            {
            case ValueType::Boolean:
                return AllocateConstantDataImpl<Boolean>(size);
            case ValueType::Char8:
                return AllocateConstantDataImpl<char>(size);
            case ValueType::Byte:
                return AllocateConstantDataImpl<uint8_t>(size);
            case ValueType::Int16:
                return AllocateConstantDataImpl<int16_t>(size);
            case ValueType::Int32:
                return AllocateConstantDataImpl<int32_t>(size);
            case ValueType::Int64:
                return AllocateConstantDataImpl<int64_t>(size);
            case ValueType::Float:
                return AllocateConstantDataImpl<float>(size);
            case ValueType::Double:
                return AllocateConstantDataImpl<double>(size);
            default:
                throw LogicException(LogicExceptionErrors::notImplemented);
            }
        }

        template <typename T, typename Data>
        ConstantData CastVector(T, const Data& data)
        {
            using VectorType = std::vector<typename T::Type>;
            return VectorType(data.begin(), data.end());
        }
    } // namespace

    ComputeContext::ComputeContext(std::string moduleName) :
        _moduleName(std::move(moduleName))
    {
        // we always have at least one stack space, in case the top level function needs to return something
        _stack.push({});
    }

    const ConstantData& ComputeContext::GetConstantData(Value value) const
    {
        if (!ValidateValue(value))
        {
            throw InputException(InputExceptionErrors::invalidArgument);
        }

        using Iterator = ConstantDataList::const_iterator;

        auto it =
            std::visit(VariantVisitor{ [](Undefined) -> Iterator { return {}; },
                                       [](Emittable) -> Iterator { return {}; },
                                       [this](auto&& data) -> Iterator {
                                           using Type = std::decay_t<decltype(data)>;
                                           using RealType = std::remove_pointer_t<Type>;
                                           using VectorType = std::vector<RealType>;

                                           const auto& frame = GetTopFrame();
                                           auto it =
                                               std::find_if(frame.second.begin(),
                                                            frame.second.end(),
                                                            [data](const ConstantData& constData) {
                                                                if (auto ptr = std::get_if<VectorType>(&constData))
                                                                {
                                                                    return ptr->data() <= data &&
                                                                           data < (ptr->data() + ptr->size());
                                                                }

                                                                return false;
                                                            });

                                           return it;
                                       } },
                       value.GetUnderlyingData());

        return *it;
    }

    Value ComputeContext::AllocateImpl(ValueType type, MemoryLayout layout)
    {
        // special case the scalar case
        auto size = layout == ScalarLayout ? 1u : layout.GetMemorySize();

        auto constantData = AllocateConstantData(type, size);
        Value value = StoreConstantData(std::move(constantData));
        value.SetLayout(layout);
        return value;
    }

    std::optional<Value> ComputeContext::GetGlobalValue(GlobalAllocationScope scope, std::string name)
    {
        std::string adjustedName = GetScopeAdjustedName(scope, name);

        if (auto it = _globals.find(adjustedName); it != _globals.end())
        {
            return ConstantDataToValue(it->second.first, it->second.second);
        }

        return std::nullopt;
    }

    Value ComputeContext::GlobalAllocateImpl(GlobalAllocationScope scope, std::string name, ConstantData data, MemoryLayout layout)
    {
        std::string adjustedName = GetScopeAdjustedName(scope, name);

        if (_globals.find(adjustedName) != _globals.end())
        {
            throw InputException(InputExceptionErrors::invalidArgument,
                                 "Unexpected collision in global data allocation");
        }

        auto& globalData = _globals[adjustedName];
        globalData.first = std::move(data);
        globalData.second = std::move(layout);

        return ConstantDataToValue(globalData.first, globalData.second);
    }

    Value ComputeContext::GlobalAllocateImpl(GlobalAllocationScope scope, std::string name, ValueType type, MemoryLayout layout)
    {
        // special case the scalar case
        auto size = layout == ScalarLayout ? 1u : layout.GetMemorySize();
        auto constantData = AllocateConstantData(type, size);
        return GlobalAllocateImpl(scope, name, constantData, layout);
    }

    Value ComputeContext::StoreConstantDataImpl(ConstantData data)
    {
        Value value = ConstantDataToValue(data);

        GetTopFrame().second.push_front(std::move(data));
        return value;
    }

    Value ComputeContext::OffsetImpl(Value begin, Value index)
    {
        if (!ValidateValue(begin))
        {
            throw InputException(InputExceptionErrors::invalidArgument);
        }

        if (!ValidateValue(index))
        {
            throw InputException(InputExceptionErrors::invalidArgument);
        }

        return std::visit(
            [&begin](auto&& index) -> Value {
                using IndexType = std::remove_pointer_t<std::decay_t<decltype(index)>>;
                if constexpr (std::is_arithmetic_v<IndexType> && !std::is_floating_point_v<IndexType>)
                {
                    auto indexValue = *index;
                    return std::visit(
                        [indexValue](auto&& begin) -> Value {
                            using BeginType = std::decay_t<decltype(begin)>;
                            if constexpr (std::is_same_v<BeginType, Emittable> || std::is_same_v<BeginType, Undefined>)
                            {
                                throw InputException(InputExceptionErrors::invalidArgument);
                            }
                            else
                            {
                                return Value(begin + indexValue);
                            }
                        },
                        begin.GetUnderlyingData());
                }
                else
                {
                    throw InputException(InputExceptionErrors::invalidArgument);
                }
            },
            index.GetUnderlyingData());
    }

    ComputeContext::ConstantData ComputeContext::ExtractConstantData(Value value)
    {
        ConstantData movedOutOfScope;

        std::visit(VariantVisitor{ [](Undefined) {},
                                   [](Emittable) {},
                                   [&movedOutOfScope, this](auto&& data) {
                                       using Type = std::decay_t<decltype(data)>;
                                       using RealType = std::remove_pointer_t<Type>;
                                       using VectorType = std::vector<RealType>;

                                       const auto& frame = GetTopFrame();
                                       if (auto stackFrameIt =
                                               std::find_if(frame.second.begin(),
                                                            frame.second.end(),
                                                            [data](const ConstantData& constData) {
                                                                if (auto ptr = std::get_if<VectorType>(&constData))
                                                                {
                                                                    return ptr->data() <= data &&
                                                                           data < (ptr->data() + ptr->size());
                                                                }

                                                                return false;
                                                            });
                                           stackFrameIt == frame.second.end())
                                       {
                                           throw LogicException(LogicExceptionErrors::illegalState,
                                                                "Could not extract expected data");
                                       }
                                       else
                                       {
                                           movedOutOfScope = std::move(*stackFrameIt);
                                       }
                                   } },
                   value.GetUnderlyingData());

        return movedOutOfScope;
    }

    bool ComputeContext::IsGlobalValue(Value value)
    {
        return std::visit(VariantVisitor{ [](Undefined) -> bool {
                                             throw LogicException(LogicExceptionErrors::illegalState);
                                         },
                                          [](Emittable) -> bool {
                                              throw LogicException(LogicExceptionErrors::illegalState);
                                          },
                                          [this](auto&& data) -> bool {
                                              using Type = std::decay_t<decltype(data)>;
                                              using RealType = std::remove_pointer_t<Type>;
                                              using VectorType = std::vector<RealType>;

                                              return std::find_if(_globals.begin(),
                                                                  _globals.end(),
                                                                  [data](const auto& kvp) {
                                                                      if (auto ptr = std::get_if<VectorType>(
                                                                              &kvp.second.first))
                                                                      {
                                                                          return ptr->data() <= data &&
                                                                                 data < (ptr->data() + ptr->size());
                                                                      }
                                                                      return false;
                                                                  }) != _globals.end();
                                          } },
                          value.GetUnderlyingData());
    }

    std::pair<ValueType, int> ComputeContext::GetTypeImpl(Emittable)
    {
        throw LogicException(LogicExceptionErrors::notImplemented);
    }

    std::function<void()> ComputeContext::CreateFunctionImpl(std::string fnName, std::function<void()> fn)
    {
        return [fn = std::move(fn), fnName = std::move(fnName), this] {
            FunctionScope scope(*this, fnName);
            fn();
        };
    }

    std::function<Value()> ComputeContext::CreateFunctionImpl(std::string fnName, Value expectedReturn, std::function<Value()> fn)
    {
        return [fn = std::move(fn), fnName = std::move(fnName), this, expectedReturn]() {
            ConstantData movedOutOfScope;
            std::optional<Value> maybeGlobal;

            {
                FunctionScope scope(*this, fnName);

                Value returnValue = expectedReturn;
                returnValue = fn();
                if (IsGlobalValue(returnValue))
                {
                    maybeGlobal = returnValue;
                }

                if (!maybeGlobal)
                {
                    movedOutOfScope = ExtractConstantData(returnValue);
                }
            }
            if (!maybeGlobal)
            {
                GetTopFrame().second.push_front(std::move(movedOutOfScope));
                return ConstantDataToValue(GetTopFrame().second.front(), expectedReturn.GetLayout());
            }
            else
            {
                return *maybeGlobal;
            }
        };
    }

    std::function<void(std::vector<Value>)> ComputeContext::CreateFunctionImpl(
        std::string fnName,
        std::vector<Value> expectedArgs,
        std::function<void(std::vector<Value>)> fn)
    {
        return [fn = std::move(fn), fnName = std::move(fnName), this, expectedArgs = std::move(expectedArgs)](
                   std::vector<Value> args) {
            assert(expectedArgs.size() == args.size());
            ConstantData movedOutOfScope;
            {
                FunctionScope scope(*this, fnName);
                auto fnArgs = expectedArgs;
                std::copy(args.begin(), args.end(), fnArgs.begin());

                fn(args);
            }
            GetTopFrame().second.push_front(std::move(movedOutOfScope));
        };
    }

    std::function<Value(std::vector<Value>)> ComputeContext::CreateFunctionImpl(
        std::string fnName,
        Value expectedReturn,
        std::vector<Value> expectedArgs,
        std::function<Value(std::vector<Value>)> fn)
    {
        return [fn = std::move(fn),
                fnName = std::move(fnName),
                this,
                expectedReturn,
                expectedArgs = std::move(expectedArgs)](std::vector<Value> args) {
            assert(expectedArgs.size() == args.size());
            ConstantData movedOutOfScope;
            std::optional<Value> maybeGlobal;
            {
                FunctionScope scope(*this, fnName);
                auto fnArgs = expectedArgs;
                std::copy(args.begin(), args.end(), fnArgs.begin());

                Value returnValue = expectedReturn;
                returnValue = fn(args);
                if (IsGlobalValue(returnValue))
                {
                    maybeGlobal = returnValue;
                }

                if (!maybeGlobal)
                {
                    movedOutOfScope = ExtractConstantData(returnValue);
                }
            }
            if (!maybeGlobal)
            {
                GetTopFrame().second.push_front(std::move(movedOutOfScope));
                return ConstantDataToValue(GetTopFrame().second.front(), expectedReturn.GetLayout());
            }
            else
            {
                return *maybeGlobal;
            }
        };
    }

    void ComputeContext::CopyDataImpl(const Value& source, Value& destination)
    {
        std::visit(VariantVisitor{ [](Undefined) {},
                                   [](Emittable) {},
                                   [&destination, &source](auto&& sourceData) {
                                       using SourceDataType = std::decay_t<decltype(sourceData)>;

                                       auto& destinationData =
                                           std::get<SourceDataType>(destination.GetUnderlyingData());
                                       if (source.GetLayout().IsContiguous() && destination.GetLayout().IsContiguous())
                                       {
                                           auto numElements = destination.GetLayout().NumElements();
                                           std::copy(sourceData, sourceData + numElements, destinationData);
                                       }
                                       else
                                       {
                                           auto& sourceLayout = source.GetLayout();
                                           auto maxCoordinate = sourceLayout.GetActiveSize().ToVector();
                                           decltype(maxCoordinate) coordinate(maxCoordinate.size());

                                           do
                                           {
                                               auto logicalCoordinates = sourceLayout.GetLogicalCoordinates(coordinate);
                                               auto sourceOffset =
                                                   sourceLayout.GetLogicalEntryOffset(logicalCoordinates);
                                               auto destinationOffset =
                                                   destination.GetLayout().GetLogicalEntryOffset(logicalCoordinates);
                                               *(destinationData + destinationOffset) = *(sourceData + sourceOffset);
                                           } while (IncrementMemoryCoordinate(coordinate, maxCoordinate));
                                       }
                                   } },
                   source.GetUnderlyingData());
    }

    void ComputeContext::MoveDataImpl(Value& source, Value& destination)
    {
        // we treat a move the same as a copy, except we clear out the source
        CopyDataImpl(source, destination);

        // data has been "moved", so clear the source
        source.Reset();
    }

    void ComputeContext::ForImpl(MemoryLayout layout, std::function<void(std::vector<Scalar>)> fn)
    {
        auto maxCoordinate = layout.GetActiveSize().ToVector();
        decltype(maxCoordinate) coordinate(maxCoordinate.size());

        do
        {
            auto logicalCoordinates = layout.GetLogicalCoordinates(coordinate).ToVector();
            fn(std::vector<Scalar>(logicalCoordinates.begin(), logicalCoordinates.end()));
        } while (IncrementMemoryCoordinate(coordinate, maxCoordinate));
    }

    Value ComputeContext::UnaryOperationImpl(ValueUnaryOperation op, Value destination)
    {
        throw LogicException(LogicExceptionErrors::notImplemented);
    }

    Value ComputeContext::BinaryOperationImpl(ValueBinaryOperation op, Value destination, Value source)
    {
        if (!ValidateValue(source))
        {
            throw InputException(InputExceptionErrors::invalidArgument);
        }
        if (!ValidateValue(destination))
        {
            destination = Allocate(source.GetBaseType(), source.GetLayout());
        }

        if (!TypeCompatible(destination, source))
        {
            throw InputException(InputExceptionErrors::typeMismatch);
        }

        if (destination.GetLayout() != source.GetLayout())
        {
            throw InputException(InputExceptionErrors::sizeMismatch);
        }

        std::visit(VariantVisitor{ [](Undefined) {},
                                   [](Emittable) {},
                                   [](Boolean*) {},
                                   [&destination, &source, op](auto&& destinationData) {
                                       using DestinationDataType =
                                           std::remove_pointer_t<std::decay_t<decltype(destinationData)>>;

                                       std::function<DestinationDataType(DestinationDataType, DestinationDataType)>
                                           opFn;
                                       switch (op)
                                       {
                                       case ValueBinaryOperation::add:
                                           opFn = [](auto dst, auto src) { return dst + src; };
                                           break;
                                       case ValueBinaryOperation::subtract:
                                           opFn = [](auto dst, auto src) { return dst - src; };
                                           break;
                                       case ValueBinaryOperation::multiply:
                                           opFn = [](auto dst, auto src) { return dst * src; };
                                           break;
                                       case ValueBinaryOperation::divide:
                                           opFn = [](auto dst, auto src) { return dst / src; };
                                           break;

                                       default:
                                           if constexpr (std::is_integral_v<DestinationDataType>)
                                           {
                                               switch (op)
                                               {
                                               case ValueBinaryOperation::modulus:
                                                   opFn = [](auto dst, auto src) { return dst % src; };
                                                   break;
                                               default:
                                                   throw LogicException(LogicExceptionErrors::illegalState);
                                               }
                                           }
                                           else
                                           {
                                               throw LogicException(LogicExceptionErrors::illegalState);
                                           }
                                       }

                                       auto& sourceData = std::get<DestinationDataType*>(source.GetUnderlyingData());
                                       if (source.GetLayout().IsContiguous() && destination.GetLayout().IsContiguous())
                                       {
                                           auto numElements = destination.GetLayout().NumElements();
                                           std::transform(destinationData,
                                                          destinationData + numElements,
                                                          sourceData,
                                                          destinationData,
                                                          opFn);
                                       }
                                       else
                                       {
                                           auto& sourceLayout = source.GetLayout();
                                           auto maxCoordinate = sourceLayout.GetActiveSize().ToVector();
                                           decltype(maxCoordinate) coordinate(maxCoordinate.size());

                                           do
                                           {
                                               auto logicalCoordinates = sourceLayout.GetLogicalCoordinates(coordinate);
                                               auto sourceOffset =
                                                   sourceLayout.GetLogicalEntryOffset(logicalCoordinates);
                                               auto destinationOffset =
                                                   destination.GetLayout().GetLogicalEntryOffset(logicalCoordinates);
                                               *(destinationData + destinationOffset) =
                                                   opFn(*(destinationData + destinationOffset),
                                                        *(sourceData + sourceOffset));
                                           } while (IncrementMemoryCoordinate(coordinate, maxCoordinate));
                                       }
                                   } },
                   destination.GetUnderlyingData());

        return destination;
    }

    Value ComputeContext::LogicalOperationImpl(ValueLogicalOperation op, Value source1, Value source2)
    {
        if (source1.GetLayout() != source2.GetLayout())
        {
            throw InputException(InputExceptionErrors::sizeMismatch);
        }

        Value returnValue =
            std::visit(VariantVisitor{ [](Undefined) -> Boolean {
                                          throw LogicException(LogicExceptionErrors::illegalState);
                                      },
                                       [](Emittable) -> Boolean {
                                           throw LogicException(LogicExceptionErrors::illegalState);
                                       },
                                       [op,
                                        &source2Data = source2.GetUnderlyingData(),
                                        &source1Layout = source1.GetLayout(),
                                        &source2Layout = source2.GetLayout()](auto&& source1) -> Boolean {
                                           using Type = std::remove_pointer_t<std::decay_t<decltype(source1)>>;

                                           std::function<bool(Type, Type)> opFn;
                                           switch (op)
                                           {
                                           case ValueLogicalOperation::equality:
                                               opFn = std::equal_to<Type>{};
                                               break;
                                           case ValueLogicalOperation::inequality:
                                               opFn = std::not_equal_to<Type>{};
                                               break;
                                           default:
                                               if constexpr (std::is_same_v<Boolean, Type>)
                                               {
                                                   throw LogicException(LogicExceptionErrors::illegalState);
                                               }
                                               else
                                               {
                                                   switch (op)
                                                   {
                                                   case ValueLogicalOperation::greaterthan:
                                                       opFn = std::greater<Type>{};
                                                       break;
                                                   case ValueLogicalOperation::greaterthanorequal:
                                                       opFn = std::greater_equal<Type>{};
                                                       break;
                                                   case ValueLogicalOperation::lessthan:
                                                       opFn = std::less<Type>{};
                                                       break;
                                                   case ValueLogicalOperation::lessthanorequal:
                                                       opFn = std::less_equal<Type>{};
                                                       break;
                                                   default:
                                                       throw LogicException(LogicExceptionErrors::illegalState);
                                                   }
                                               }
                                           }

                                           auto maxCoordinate = source1Layout.GetActiveSize().ToVector();
                                           decltype(maxCoordinate) coordinate(maxCoordinate.size());

                                           bool b = true;
                                           auto source2 = std::get<Type*>(source2Data);
                                           do
                                           {
                                               auto logicalCoordinates =
                                                   source1Layout.GetLogicalCoordinates(coordinate);
                                               auto source1Offset =
                                                   source1Layout.GetLogicalEntryOffset(logicalCoordinates);
                                               auto source2Offset =
                                                   source2Layout.GetLogicalEntryOffset(logicalCoordinates);
                                               b &= opFn(source1[source1Offset], source2[source2Offset]);
                                           } while (IncrementMemoryCoordinate(coordinate, maxCoordinate));

                                           return b;
                                       } },
                       source1.GetUnderlyingData());

        return returnValue;
    }

    Value ComputeContext::CastImpl(Value value, ValueType destType)
    {
        if (!ValidateValue(value))
        {
            throw InputException(InputExceptionErrors::invalidArgument);
        }

        ConstantData castedData;

        std::visit(VariantVisitor{ [](Undefined) {},
                                   [](Emittable) {},
                                   [&castedData, destType, &value](auto&& data) {
                                       auto ptrBegin = data;
                                       auto ptrEnd = data + value.GetLayout().GetMemorySize();

                                       switch (destType)
                                       {
                                       case ValueType::Boolean:
                                           castedData = std::vector<Boolean>(ptrBegin, ptrEnd);
                                           break;
                                       case ValueType::Char8:
                                           castedData = std::vector<char>(ptrBegin, ptrEnd);
                                           break;
                                       case ValueType::Byte:
                                           castedData = std::vector<uint8_t>(ptrBegin, ptrEnd);
                                           break;
                                       case ValueType::Int16:
                                           castedData = std::vector<int16_t>(ptrBegin, ptrEnd);
                                           break;
                                       case ValueType::Int32:
                                           castedData = std::vector<int32_t>(ptrBegin, ptrEnd);
                                           break;
                                       case ValueType::Int64:
                                           castedData = std::vector<int64_t>(ptrBegin, ptrEnd);
                                           break;
                                       case ValueType::Float:
                                           castedData = std::vector<float>(ptrBegin, ptrEnd);
                                           break;
                                       case ValueType::Double:
                                           castedData = std::vector<double>(ptrBegin, ptrEnd);
                                           break;
                                       default:
                                           throw LogicException(LogicExceptionErrors::notImplemented);
                                       }
                                   } },
                   value.GetUnderlyingData());

        Value castedValue = StoreConstantData(std::move(castedData));
        castedValue.SetLayout(value.GetLayout());
        return castedValue;
    }

    class ComputeContext::IfContextImpl : public EmitterContext::IfContextImpl
    {
    public:
        IfContextImpl(bool state) :
            _state(state) {}

        void ElseIf(Scalar test, std::function<void()> fn) override
        {
            if (!test.GetValue().IsConstant())
            {
                throw LogicException(LogicExceptionErrors::illegalState);
            }

            if (!_state && test.Get<Boolean>())
            {
                fn();
                _state = !_state;
            }
        }

        void Else(std::function<void()> fn) override
        {
            if (!_state)
            {
                fn();
                _state = !_state;
            }
        }

    private:
        bool _state;
    };

    EmitterContext::IfContext ComputeContext::IfImpl(Scalar test, std::function<void()> fn)
    {
        if (!test.GetValue().IsConstant())
        {
            throw LogicException(LogicExceptionErrors::illegalState);
        }

        bool state = test.Get<Boolean>();
        if (state)
        {
            fn();
        }

        return { std::make_unique<ComputeContext::IfContextImpl>(state) };
    }

    Value ComputeContext::CallImpl(std::string fnName, Value retValue, std::vector<Value> args)
    {
        static std::map<std::string, std::function<Value(std::vector<Value>)>> fnMap{
            { "abs",
              [](std::vector<Value> args) -> Value {
                  if (args.size() != 1)
                  {
                      throw InputException(InputExceptionErrors::invalidArgument);
                  }
                  return std::visit([](auto&&) { return Value{}; }, args[0].GetUnderlyingData());
              } }
        };

        if (!std::all_of(args.begin(), args.end(), [this](const Value& value) { return ValidateValue(value); }))
        {
            throw LogicException(LogicExceptionErrors::illegalState);
        }

        if (auto it = fnMap.find(fnName); it != fnMap.end())
        {
            return it->second(args);
        }

        throw LogicException(LogicExceptionErrors::notImplemented);
    }

    bool ComputeContext::ValidateValue(Value value) const
    {
        return value.IsDefined() && !value.IsEmpty() && value.IsConstant();
    }

    bool ComputeContext::TypeCompatible(Value value1, Value value2) const
    {
        return value1.GetBaseType() == value2.GetBaseType() && value1.PointerLevel() == value2.PointerLevel();
    }

    std::string ComputeContext::GetScopeAdjustedName(GlobalAllocationScope scope, std::string name) const
    {
        switch (scope)
        {
        case GlobalAllocationScope::Global:
            return GetGlobalScopedName(name);
        case GlobalAllocationScope::Function:
            return GetCurrentFunctionScopedName(name);
        }

        throw LogicException(LogicExceptionErrors::illegalState);
    }

    std::string ComputeContext::GetGlobalScopedName(std::string name) const { return _moduleName + "_" + name; }

    std::string ComputeContext::GetCurrentFunctionScopedName(std::string name) const
    {
        // Our stack always has one empty "scope" pushed to it, which we
        // can use to create our global prefix.

        return GetGlobalScopedName(GetTopFrame().first + "_" + name);
    }

    ComputeContext::Frame& ComputeContext::GetTopFrame() { return _stack.top(); }

    const ComputeContext::Frame& ComputeContext::GetTopFrame() const { return _stack.top(); }

} // namespace value
} // namespace ell
