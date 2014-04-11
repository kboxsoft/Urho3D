//
// Copyright (c) 2008-2014 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "Precompiled.h"
#include "Animatable.h"
#include "AttributeAnimation.h"
#include "Context.h"
#include "Deserializer.h"
#include "Log.h"
#include "ObjectAnimation.h"
#include "Serializer.h"
#include "XMLFile.h"

#include "DebugNew.h"

namespace Urho3D
{

    AttributeAnimation::AttributeAnimation(Context* context) :
Resource(context),
    interpolationMethod_(IM_LINEAR),
    valueType_(VAR_NONE),
    isInterpolatable_(false),
    beginTime_(M_INFINITY),
    endTime_(-M_INFINITY),
    splineTension_(0.5f),
    splineTangentsDirty_(false)
{
}

AttributeAnimation::~AttributeAnimation()
{
}

void AttributeAnimation::RegisterObject(Context* context)
{
    context->RegisterFactory<AttributeAnimation>();
}

bool AttributeAnimation::Load(Deserializer& source)
{
    XMLFile xmlFile(context_);
    if (!xmlFile.Load(source))
        return false;

    return LoadXML(xmlFile.GetRoot());
}

bool AttributeAnimation::Save(Serializer& dest) const
{
    XMLFile xmlFile(context_);

    XMLElement rootElem = xmlFile.CreateRoot("attributeanimation");
    if (!SaveXML(rootElem))
        return false;

    return xmlFile.Save(dest);
}

bool AttributeAnimation::LoadXML(const XMLElement& source)
{
    valueType_ = VAR_NONE;
    eventFrames_.Clear();

    XMLElement keyFrameEleme = source.GetChild("keyframe");
    while (keyFrameEleme)
    {
        float time = keyFrameEleme.GetFloat("time");
        Variant value = keyFrameEleme.GetVariant();
        SetKeyFrame(time, value);

        keyFrameEleme = keyFrameEleme.GetNext("keyframe");
    }

    XMLElement eventFrameElem = source.GetChild("eventframe");
    while (eventFrameElem)
    {
        float time = eventFrameElem.GetFloat("time");
        unsigned eventType = eventFrameElem.GetUInt("eventtype");
        VariantMap eventData = eventFrameElem.GetChild("eventdata").GetVariantMap();

        SetEventFrame(time, StringHash(eventType), eventData);
        eventFrameElem = eventFrameElem.GetNext("eventframe");
    }

    return true;
}

bool AttributeAnimation::SaveXML(XMLElement& dest) const
{
    for (unsigned i = 0; i < keyFrames_.Size(); ++i)
    {
        const AttributeKeyFrame& keyFrame = keyFrames_[i];
        XMLElement keyFrameEleme = dest.CreateChild("keyframe");
        keyFrameEleme.SetFloat("time", keyFrame.time_);
        keyFrameEleme.SetVariant(keyFrame.value_);
    }

    for (unsigned i = 0; i < eventFrames_.Size(); ++i)
    {
        const AttributeEventFrame& eventFrame = eventFrames_[i];
        XMLElement eventFrameElem = dest.CreateChild("eventframe");
        eventFrameElem.SetFloat("time", eventFrame.time_);
        eventFrameElem.SetUInt("eventtype", eventFrame.eventType_.Value());
        eventFrameElem.CreateChild("eventdata").SetVariantMap(eventFrame.eventData_);
    }

    return true;
}

void AttributeAnimation::SetObjectAnimation(ObjectAnimation* objectAnimation)
{
    objectAnimation_ = objectAnimation;
}

void AttributeAnimation::SetValueType(VariantType valueType)
{
    if (valueType == valueType_)
        return;

    valueType_ = valueType;
    isInterpolatable_ = (valueType_ == VAR_FLOAT) || (valueType_ == VAR_VECTOR2) || (valueType_ == VAR_VECTOR3) || (valueType_ == VAR_VECTOR4) || 
        (valueType_ == VAR_QUATERNION) || (valueType_ == VAR_COLOR);

    if ((valueType_ == VAR_INTRECT) || (valueType_ == VAR_INTVECTOR2))
    {
        isInterpolatable_ = true;
        // Force linear interpolation for IntRect and IntVector2
        interpolationMethod_ = IM_LINEAR;
    }

    keyFrames_.Clear();
    beginTime_ = M_INFINITY;
    endTime_ = -M_INFINITY;
}

void AttributeAnimation::SetInterpolationMethod(InterpolationMethod method)
{
    if (method == interpolationMethod_)
        return;

    // Force linear interpolation for IntRect and IntVector2
    if ((valueType_ == VAR_INTRECT) || (valueType_ == VAR_INTVECTOR2))
        method = IM_LINEAR;

    interpolationMethod_ = method;
    splineTangentsDirty_ = true;
}

void AttributeAnimation::SetSplineTension(float tension)
{
    splineTension_ = tension;
    splineTangentsDirty_ = true;
}

bool AttributeAnimation::SetKeyFrame(float time, const Variant& value)
{
    if (valueType_ == VAR_NONE)
        SetValueType(value.GetType());
    else if (value.GetType() != valueType_)
        return false;

    beginTime_ = Min(time, beginTime_);
    endTime_ = Max(time, endTime_);

    AttributeKeyFrame keyFrame;
    keyFrame.time_ = time;
    keyFrame.value_ = value;

    if (keyFrames_.Empty() || time >= keyFrames_.Back().time_)
        keyFrames_.Push(keyFrame);
    else
    {
        for (unsigned i = 0; i < keyFrames_.Size(); ++i)
        {
            if (time < keyFrames_[i].time_)
            {
                keyFrames_.Insert(i, keyFrame);
                break;
            }
        }
    }

    splineTangentsDirty_ = true;

    return true;
}

void AttributeAnimation::SetEventFrame(float time, const StringHash& eventType, const VariantMap& eventData)
{
    AttributeEventFrame eventFrame;
    eventFrame.time_ = time;
    eventFrame.eventType_ = eventType;
    eventFrame.eventData_ = eventData;

    if (eventFrames_.Empty() || time >= eventFrames_.Back().time_)
        eventFrames_.Push(eventFrame);
    else
    {
        for (unsigned i = 0; i < eventFrames_.Size(); ++i)
        {
            if (time < eventFrames_[i].time_)
            {
                eventFrames_.Insert(i, eventFrame);
                break;
            }
        }
    }
}

bool AttributeAnimation::IsValid() const
{
    return (interpolationMethod_ == IM_LINEAR && keyFrames_.Size() > 1) || (interpolationMethod_ == IM_SPLINE && keyFrames_.Size() > 2);
}

ObjectAnimation* AttributeAnimation::GetObjectAnimation() const
{
    return objectAnimation_;
}

void AttributeAnimation::UpdateAttributeValue(Animatable* animatable, const AttributeInfo& attributeInfo, float scaledTime)
{
    unsigned index = 1;
    for (; index < keyFrames_.Size(); ++index)
    {
        if (scaledTime < keyFrames_[index].time_)
            break;
    }

    if (index >= keyFrames_.Size() || !isInterpolatable_)
        animatable->OnSetAttribute(attributeInfo, keyFrames_[index - 1].value_);
    else
    {
        if (interpolationMethod_ == IM_LINEAR)
            animatable->OnSetAttribute(attributeInfo, LinearInterpolation(index - 1, index, scaledTime));
        else
            animatable->OnSetAttribute(attributeInfo, SplineInterpolation(index - 1, index, scaledTime));
    }
}

void AttributeAnimation::GetEventFrames(float beginTime, float endTime, Vector<const AttributeEventFrame*>& eventFrames) const
{
    for (unsigned i = 0; i < eventFrames_.Size(); ++i)
    {
        const AttributeEventFrame& eventFrame = eventFrames_[i];
        if (eventFrame.time_ >= endTime)
            break;

        if (eventFrame.time_ >= beginTime)
            eventFrames.Push(&eventFrame);
    }
}

Variant AttributeAnimation::LinearInterpolation(unsigned index1, unsigned index2, float scaledTime) const
{
    const AttributeKeyFrame& keyFrame1 = keyFrames_[index1];
    const AttributeKeyFrame& keyFrame2 = keyFrames_[index2];

    float t = (scaledTime - keyFrame1.time_) / (keyFrame2.time_ - keyFrame1.time_);
    return LinearInterpolation(keyFrame1.value_, keyFrame2.value_, t);
}

Variant AttributeAnimation::LinearInterpolation(const Variant& value1, const Variant& value2, float t) const
{
    switch (valueType_)
    {
    case VAR_FLOAT:
        return Lerp(value1.GetFloat(), value2.GetFloat(), t);

    case VAR_VECTOR2:
        return value1.GetVector2().Lerp(value2.GetVector2(), t);

    case VAR_VECTOR3:
        return value1.GetVector3().Lerp(value2.GetVector3(), t);

    case VAR_VECTOR4:
        return value1.GetVector4().Lerp(value2.GetVector4(), t);

    case VAR_QUATERNION:
        return value1.GetQuaternion().Slerp(value2.GetQuaternion(), t);

    case VAR_COLOR:
        return value1.GetColor().Lerp(value2.GetColor(), t);

    case VAR_INTRECT:
        {
            float s = 1.0f - t;
            const IntRect& r1 = value1.GetIntRect();
            const IntRect& r2 = value2.GetIntRect();
            return IntRect((int)(r1.left_ * s + r2.left_ * t), (int)(r1.top_ * s + r2.top_ * t), (int)(r1.right_ * s + r2.right_ * t), (int)(r1.bottom_ * s + r2.bottom_ * t));
        }

    case VAR_INTVECTOR2:
        {
            float s = 1.0f - t;
            const IntVector2& v1 = value1.GetIntVector2();
            const IntVector2& v2 = value2.GetIntVector2();
            return IntVector2((int)(v1.x_ * s + v2.x_ * t), (int)(v1.y_ * s + v2.y_ * t));
        }
    }

    LOGERROR("Invalid value type for linear interpolation");
    return Variant::EMPTY;
}

Variant AttributeAnimation::SplineInterpolation(unsigned index1, unsigned index2, float scaledTime)
{
    if (splineTangentsDirty_)
        UpdateSplineTangents();

    const AttributeKeyFrame& keyFrame1 = keyFrames_[index1];
    const AttributeKeyFrame& keyFrame2 = keyFrames_[index2];

    float t = (scaledTime - keyFrame1.time_) / (keyFrame2.time_ - keyFrame1.time_);

    float tt = t * t;
    float ttt = t * tt;

    float h1 = 2.0f * ttt - 3.0f * tt + 1.0f;
    float h2 = -2.0f * ttt + 3.0f * tt;
    float h3 = ttt - 2.0f * tt + t;
    float h4 = ttt - tt;

    const Variant& v1 = keyFrame1.value_;
    const Variant& v2 = keyFrame2.value_;
    const Variant& t1 = splineTangents_[index1];
    const Variant& t2 = splineTangents_[index2];

    switch (valueType_)
    {
    case VAR_FLOAT:
        return v1.GetFloat() * h1 + v2.GetFloat() * h2 + t1.GetFloat() * h3 + t2.GetFloat() * h4;

    case VAR_VECTOR2:
        return v1.GetVector2() * h1 + v2.GetVector2() * h2 + t1.GetVector2() * h3 + t2.GetVector2() * h4;

    case VAR_VECTOR3:
        return v1.GetVector3() * h1 + v2.GetVector3() * h2 + t1.GetVector3() * h3 + t2.GetVector3() * h4;

    case VAR_VECTOR4:
        return v1.GetVector4() * h1 + v2.GetVector4() * h2 + t1.GetVector4() * h3 + t2.GetVector4() * h4;

    case VAR_QUATERNION:
        return v1.GetQuaternion() * h1 + v2.GetQuaternion() * h2 + t1.GetQuaternion() * h3 + t2.GetQuaternion() * h4;

    case VAR_COLOR:
        return v1.GetColor() * h1 + v2.GetColor() * h2 + t1.GetColor() * h3 + t2.GetColor() * h4;
    }

    LOGERROR("Invalid value type for spline interpolation");
    return Variant::EMPTY;
}

void AttributeAnimation::UpdateSplineTangents()
{
    splineTangents_.Clear();

    if (!IsValid())
        return;

    unsigned size = keyFrames_.Size();
    splineTangents_.Resize(size);

    for (unsigned i = 1; i < size - 1; ++i)
        splineTangents_[i] = SubstractAndMultiply(keyFrames_[i + 1].value_, keyFrames_[i - 1].value_, splineTension_);

    // Make end point's tangent zero
    splineTangents_[0] = splineTangents_[size - 1] = SubstractAndMultiply(keyFrames_[0].value_, keyFrames_[0].value_, splineTension_);

    splineTangentsDirty_ = false;
}

Variant AttributeAnimation::SubstractAndMultiply(const Variant& value1, const Variant& value2, float t) const
{
    switch (valueType_)
    {
    case VAR_FLOAT:
        return (value1.GetFloat() - value2.GetFloat()) * t;

    case VAR_VECTOR2:
        return (value1.GetVector2() - value2.GetVector2()) * t;

    case VAR_VECTOR3:
        return (value1.GetVector3() - value2.GetVector3()) * t;

    case VAR_VECTOR4:
        return (value1.GetVector4() - value2.GetVector4()) * t;

    case VAR_QUATERNION:
        return (value1.GetQuaternion() - value2.GetQuaternion()) * t;

    case VAR_COLOR:
        return (value1.GetColor() - value2.GetColor()) * t;
    }
    
    LOGERROR("Invalid value type for spline interpolation");
    return Variant::EMPTY;
}

}