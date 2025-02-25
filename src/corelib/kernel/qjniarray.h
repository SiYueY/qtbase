// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QJNIARRAY_H
#define QJNIARRAY_H

#include <QtCore/qlist.h>

#if defined(Q_QDOC) || defined(Q_OS_ANDROID)
#include <QtCore/qbytearray.h>
#include <QtCore/qjniobject.h>

#include <iterator>
#include <utility>
#include <QtCore/q20type_traits.h>

#if defined(Q_QDOC)
using jsize = qint32;
using jarray = jobject;
#endif

QT_BEGIN_NAMESPACE

template <typename T> class QJniArray;
template <typename T>
struct QJniArrayIterator
{
    QJniArrayIterator() = default;

    constexpr QJniArrayIterator(const QJniArrayIterator &other) noexcept = default;
    constexpr QJniArrayIterator(QJniArrayIterator &&other) noexcept = default;
    constexpr QJniArrayIterator &operator=(const QJniArrayIterator &other) noexcept = default;
    constexpr QJniArrayIterator &operator=(QJniArrayIterator &&other) noexcept = default;

    using difference_type = jsize;
    using value_type = T;
    using pointer = T *;
    using reference = T; // difference to container requirements
    using const_reference = reference;
    using iterator_category = std::bidirectional_iterator_tag;

    const_reference operator*() const
    {
        return m_array->at(m_index);
    }
    friend QJniArrayIterator &operator++(QJniArrayIterator &that) noexcept
    {
        ++that.m_index;
        return that;
    }
    friend QJniArrayIterator operator++(QJniArrayIterator &that, int) noexcept
    {
        auto copy = that;
        ++that;
        return copy;
    }
    friend QJniArrayIterator &operator--(QJniArrayIterator &that) noexcept
    {
        --that.m_index;
        return that;
    }
    friend QJniArrayIterator operator--(QJniArrayIterator &that, int) noexcept
    {
        auto copy = that;
        --that;
        return copy;
    }
    void swap(QJniArrayIterator &other) noexcept
    {
        std::swap(m_index, other.m_index);
        qt_ptr_swap(m_array, other.m_array);
    }

private:
    friend constexpr bool comparesEqual(const QJniArrayIterator &lhs,
                                        const QJniArrayIterator &rhs)
    {
        Q_ASSERT(lhs.m_array == rhs.m_array);
        return lhs.m_index == rhs.m_index;
    }
    Q_DECLARE_EQUALITY_COMPARABLE(QJniArrayIterator)

    using VT = std::remove_const_t<T>;
    friend class QJniArray<VT>;

    qsizetype m_index = 0;
    const QJniArray<VT> *m_array = nullptr;

    QJniArrayIterator(qsizetype index, const QJniArray<VT> *array)
        : m_index(index), m_array(array)
    {}
};

class QJniArrayBase
{
    // for SFINAE'ing out the fromContainer named constructor
    template <typename C, typename = void> struct IsContiguousContainerHelper : std::false_type {};
    template <typename C>
    struct IsContiguousContainerHelper<C, std::void_t<decltype(std::data(std::declval<C>())),
                                                      decltype(std::size(std::declval<C>())),
                                                      typename C::value_type
                                                     >
                                      > : std::true_type {};

public:
    using size_type = jsize;
    using difference_type = size_type;

    operator QJniObject() const { return m_object; }

    template <typename T = jobject>
    T object() const { return m_object.object<T>(); }
    bool isValid() const { return m_object.isValid(); }
    bool isEmpty() const { return size() == 0; }

    size_type size() const
    {
        if (jarray array = m_object.object<jarray>())
            return jniEnv()->GetArrayLength(array);
        return 0;
    }

    template <typename C>
    static constexpr bool isContiguousContainer = IsContiguousContainerHelper<q20::remove_cvref_t<C>>::value;
    template <typename C>
    using if_contiguous_container = std::enable_if_t<isContiguousContainer<C>, bool>;

    template <typename Container, if_contiguous_container<Container> = true>
    static auto fromContainer(Container &&container)
    {
        Q_ASSERT_X(size_t(std::size(container)) <= size_t((std::numeric_limits<size_type>::max)()),
                   "QJniArray::fromContainer", "Container is too large for a Java array");

        using ElementType = typename std::remove_reference_t<Container>::value_type;
        if constexpr (std::disjunction_v<std::is_same<ElementType, jobject>,
                                         std::is_same<ElementType, QJniObject>,
                                         std::is_same<ElementType, QString>,
                                         std::is_base_of<QtJniTypes::JObjectBase, ElementType>
                                        >) {
            return makeObjectArray(std::forward<Container>(container));
        } else if constexpr (std::is_same_v<ElementType, jfloat>) {
            return makeArray<jfloat>(std::forward<Container>(container), &JNIEnv::NewFloatArray,
                                                             &JNIEnv::SetFloatArrayRegion);
        } else if constexpr (std::is_same_v<ElementType, jdouble>) {
            return makeArray<jdouble>(std::forward<Container>(container), &JNIEnv::NewDoubleArray,
                                                              &JNIEnv::SetDoubleArrayRegion);
        } else if constexpr (std::disjunction_v<std::is_same<ElementType, jboolean>,
                                                std::is_same<ElementType, bool>>) {
            return makeArray<jboolean>(std::forward<Container>(container), &JNIEnv::NewBooleanArray,
                                                               &JNIEnv::SetBooleanArrayRegion);
        } else if constexpr (std::disjunction_v<std::is_same<ElementType, jbyte>,
                                                std::is_same<ElementType, char>>) {
            return makeArray<jbyte>(std::forward<Container>(container), &JNIEnv::NewByteArray,
                                                            &JNIEnv::SetByteArrayRegion);
        } else if constexpr (std::disjunction_v<std::is_same<ElementType, jchar>,
                                                std::is_same<ElementType, QChar>>) {
            return makeArray<jchar>(std::forward<Container>(container), &JNIEnv::NewCharArray,
                                                            &JNIEnv::SetCharArrayRegion);
        } else if constexpr (std::is_same_v<ElementType, jshort>
                          || sizeof(ElementType) == sizeof(jshort)) {
            return makeArray<jshort>(std::forward<Container>(container), &JNIEnv::NewShortArray,
                                                             &JNIEnv::SetShortArrayRegion);
        } else if constexpr (std::is_same_v<ElementType, jint>
                          || sizeof(ElementType) == sizeof(jint)) {
            return makeArray<jint>(std::forward<Container>(container), &JNIEnv::NewIntArray,
                                                           &JNIEnv::SetIntArrayRegion);
        } else if constexpr (std::is_same_v<ElementType, jlong>
                          || sizeof(ElementType) == sizeof(jlong)) {
            return makeArray<jlong>(std::forward<Container>(container), &JNIEnv::NewLongArray,
                                                            &JNIEnv::SetLongArrayRegion);
        }
    }

protected:
    QJniArrayBase() = default;
    ~QJniArrayBase() = default;

    explicit QJniArrayBase(jarray array)
        : m_object(static_cast<jobject>(array))
    {
    }
    explicit QJniArrayBase(const QJniObject &object)
        : m_object(object)
    {}
    explicit QJniArrayBase(QJniObject &&object) noexcept
        : m_object(std::move(object))
    {}

    JNIEnv *jniEnv() const noexcept { return QJniEnvironment::getJniEnv(); }

    template <typename ElementType, typename List, typename NewFn, typename SetFn>
    static auto makeArray(List &&list, NewFn &&newArray, SetFn &&setRegion);
    template <typename List>
    static auto makeObjectArray(List &&list);

private:
    QJniObject m_object;
};

template <typename T>
class QJniArray : public QJniArrayBase
{
    friend struct QJniArrayIterator<T>;
public:
    using Type = T;

    using value_type = T;
    using reference = T;
    using const_reference = const reference;

    // read-only container, so no iterator typedef
    using const_iterator = QJniArrayIterator<const T>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    QJniArray() = default;
    explicit QJniArray(jarray array) : QJniArrayBase(array) {}
    explicit QJniArray(const QJniObject &object) : QJniArrayBase(object) {}
    explicit QJniArray(QJniObject &&object) noexcept : QJniArrayBase(std::move(object)) {}

    // base class destructor is protected, so need to provide all SMFs
    QJniArray(const QJniArray &other) = default;
    QJniArray(QJniArray &&other) noexcept = default;
    QJniArray &operator=(const QJniArray &other) = default;
    QJniArray &operator=(QJniArray &&other) noexcept = default;

    template <typename Container, if_contiguous_container<Container> = true>
    explicit QJniArray(Container &&container)
        : QJniArrayBase(QJniArrayBase::fromContainer(std::forward<Container>(container)))
    {
    }

    Q_IMPLICIT inline QJniArray(std::initializer_list<T> list)
        : QJniArrayBase(QJniArrayBase::fromContainer(list))
    {
    }

    template <typename Other>
    using if_convertible = std::enable_if_t<std::is_convertible_v<Other, T>, bool>;

    template <typename Other, if_convertible<Other> = true>
    QJniArray(QJniArray<Other> &&other)
        : QJniArrayBase(std::forward<QJniArray<Other>>(other))
    {
    }
    ~QJniArray() = default;

    auto arrayObject() const
    {
        if constexpr (std::is_convertible_v<jobject, T>)
            return object<jobjectArray>();
        else if constexpr (std::is_same_v<T, jbyte>)
            return object<jbyteArray>();
        else if constexpr (std::is_same_v<T, jchar>)
            return object<jcharArray>();
        else if constexpr (std::is_same_v<T, jboolean>)
            return object<jbooleanArray>();
        else if constexpr (std::is_same_v<T, jshort>)
            return object<jshortArray>();
        else if constexpr (std::is_same_v<T, jint>)
            return object<jintArray>();
        else if constexpr (std::is_same_v<T, jlong>)
            return object<jlongArray>();
        else if constexpr (std::is_same_v<T, jfloat>)
            return object<jfloatArray>();
        else if constexpr (std::is_same_v<T, jdouble>)
            return object<jdoubleArray>();
        else
            return object<jarray>();
    }

    const_iterator begin() const noexcept { return {0, this}; }
    const_iterator constBegin() const noexcept { return begin(); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator end() const noexcept { return {size(), this}; }
    const_iterator constEnd() const noexcept { return {end()}; }
    const_iterator cend() const noexcept { return {end()}; }

    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

    const_reference operator[](size_type i) const { return at(i); }
    const_reference at(size_type i) const
    {
        JNIEnv *env = jniEnv();
        if constexpr (std::is_convertible_v<jobject, T>) {
            jobject element = env->GetObjectArrayElement(object<jobjectArray>(), i);
            if constexpr (std::is_base_of_v<QJniObject, T>)
                return QJniObject::fromLocalRef(element);
            else if constexpr (std::is_base_of_v<QtJniTypes::JObjectBase, T>)
                return T::fromLocalRef(element);
            else
                return T{element};
        } else if constexpr (std::is_base_of_v<std::remove_pointer_t<jobject>, std::remove_pointer_t<T>>) {
            // jstring, jclass etc
            return static_cast<T>(env->GetObjectArrayElement(object<jobjectArray>(), i));
        } else {
            T res = {};
            if constexpr (std::is_same_v<T, jbyte>)
                env->GetByteArrayRegion(object<jbyteArray>(), i, 1, &res);
            else if constexpr (std::is_same_v<T, jchar>)
                env->GetCharArrayRegion(object<jcharArray>(), i, 1, &res);
            else if constexpr (std::is_same_v<T, jboolean>)
                env->GetBooleanArrayRegion(object<jbooleanArray>(), i, 1, &res);
            else if constexpr (std::is_same_v<T, jshort>)
                env->GetShortArrayRegion(object<jshortArray>(), i, 1, &res);
            else if constexpr (std::is_same_v<T, jint>)
                env->GetIntArrayRegion(object<jbyteArray>(), i, 1, &res);
            else if constexpr (std::is_same_v<T, jlong>)
                env->GetLongArrayRegion(object<jlongArray>(), i, 1, &res);
            else if constexpr (std::is_same_v<T, jfloat>)
                env->GetFloatArrayRegion(object<jfloatArray>(), i, 1, &res);
            else if constexpr (std::is_same_v<T, jdouble>)
                env->GetDoubleArrayRegion(object<jdoubleArray>(), i, 1, &res);
            return res;
        }
    }

    auto toContainer() const
    {
        JNIEnv *env = jniEnv();
        if constexpr (std::is_same_v<T, jobject>) {
            QList<jobject> res;
            res.reserve(size());
            for (auto element : *this)
                res.append(element);
            return res;
        } else if constexpr (std::is_same_v<T, jstring>) {
            QStringList res;
            res.reserve(size());
            for (auto element : *this)
                res.append(QJniObject(element).toString());
            return res;
        } else if constexpr (std::is_same_v<T, jbyte>) {
            const qsizetype bytecount = size();
            QByteArray res(bytecount, Qt::Initialization::Uninitialized);
            if (!isEmpty()) {
                env->GetByteArrayRegion(object<jbyteArray>(),
                                        0, bytecount, reinterpret_cast<jbyte *>(res.data()));
            }
            return res;
        } else {
            QList<T> res;
            if (isEmpty())
                return res;
            res.resize(size());
            if constexpr (std::is_same_v<T, jchar>) {
                env->GetCharArrayRegion(object<jcharArray>(),
                                        0, res.size(), res.data());
            } else if constexpr (std::is_same_v<T, jboolean>) {
                env->GetBooleanArrayRegion(object<jbooleanArray>(),
                                           0, res.size(), res.data());
            } else if constexpr (std::is_same_v<T, jshort>) {
                env->GetShortArrayRegion(object<jshortArray>(),
                                         0, res.size(), res.data());
            } else if constexpr (std::is_same_v<T, jint>) {
                env->GetIntArrayRegion(object<jintArray>(),
                                       0, res.size(), res.data());
            } else if constexpr (std::is_same_v<T, jlong>) {
                env->GetLongArrayRegion(object<jlongArray>(),
                                        0, res.size(), res.data());
            } else if constexpr (std::is_same_v<T, jfloat>) {
                env->GetFloatArrayRegion(object<jfloatArray>(),
                                         0, res.size(), res.data());
            } else if constexpr (std::is_same_v<T, jdouble>) {
                env->GetDoubleArrayRegion(object<jdoubleArray>(),
                                          0, res.size(), res.data());
            } else {
                res.clear();
            }
            return res;
        }
    }
};

// Deduction guide so that we can construct as 'QJniArray list(Container<T>)'. Since
// fromContainer() maps several C++ types to the same JNI type (e.g. both jboolean and
// bool become QJniArray<jboolean>), we have to deduce to what fromContainer() would
// give us.
template <typename Container, QJniArrayBase::if_contiguous_container<Container> = true>
QJniArray(Container) -> QJniArray<typename decltype(QJniArrayBase::fromContainer(std::declval<Container>()))::value_type>;

template <typename ElementType, typename List, typename NewFn, typename SetFn>
auto QJniArrayBase::makeArray(List &&list, NewFn &&newArray, SetFn &&setRegion)
{
    const size_type length = size_type(std::size(list));
    JNIEnv *env = QJniEnvironment::getJniEnv();
    auto localArray = (env->*newArray)(length);
    if (QJniEnvironment::checkAndClearExceptions(env))
        return QJniArray<ElementType>();

    // can't use static_cast here because we have signed/unsigned mismatches
    if (length) {
        (env->*setRegion)(localArray, 0, length,
                          reinterpret_cast<const ElementType *>(std::data(std::as_const(list))));
    }
    return QJniArray<ElementType>(localArray);
};

template <typename List>
auto QJniArrayBase::makeObjectArray(List &&list)
{
    using ElementType = typename q20::remove_cvref_t<List>::value_type;
    using ResultType = QJniArray<decltype(std::declval<QJniObject::LocalFrame<>>().convertToJni(
                                    std::declval<ElementType>()))
                                >;

    if (std::size(list) == 0)
        return ResultType();

    JNIEnv *env = QJniEnvironment::getJniEnv();
    const size_type length = size_type(std::size(list));

    // this assumes that all objects in the list have the same class
    jclass elementClass = nullptr;
    if constexpr (std::disjunction_v<std::is_same<ElementType, QJniObject>,
                                     std::is_base_of<QtJniTypes::JObjectBase, ElementType>>) {
        elementClass = std::begin(list)->objectClass();
    } else if constexpr (std::is_same_v<ElementType, QString>) {
        elementClass = env->FindClass("java/lang/String");
    } else {
        elementClass = env->GetObjectClass(*std::begin(list));
    }
    auto localArray = env->NewObjectArray(length, elementClass, nullptr);
    if (QJniEnvironment::checkAndClearExceptions(env))
        return ResultType();

    // explicitly manage the frame for local references in chunks of 100
    QJniObject::LocalFrame frame(env);
    constexpr jint frameCapacity = 100;
    qsizetype i = 0;
    for (const auto &element : std::as_const(list)) {
        if (i % frameCapacity == 0) {
            if (i)
                env->PopLocalFrame(nullptr);
            if (env->PushLocalFrame(frameCapacity) != 0)
                return ResultType{};
        }
        jobject object = frame.convertToJni(element);
        env->SetObjectArrayElement(localArray, i, object);
        ++i;
    }
    if (i)
        env->PopLocalFrame(nullptr);
    return ResultType(localArray);
}

namespace QtJniTypes
{
template <typename T> struct IsJniArray: std::false_type {};
template <typename T> struct IsJniArray<QJniArray<T>> : std::true_type {};
template <typename T> struct Traits<QJniArray<T>> {
    template <IfValidFieldType<T> = true>
    static constexpr auto signature()
    {
        return CTString("[") + Traits<T>::signature();
    }
};
template <typename T> struct Traits<QList<T>> {
    template <IfValidFieldType<T> = true>
    static constexpr auto signature()
    {
        return CTString("[") + Traits<T>::signature();
    }
};
template <> struct Traits<QByteArray> {
    static constexpr auto signature()
    {
        return CTString("[B");
    }
};
}

QT_END_NAMESPACE

#endif

#endif // QJNIARRAY_H
