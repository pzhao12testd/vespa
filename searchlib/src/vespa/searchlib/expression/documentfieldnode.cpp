// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#include "documentfieldnode.h"
#include "getdocidnamespacespecificfunctionnode.h"
#include "getymumchecksumfunctionnode.h"
#include <vespa/document/fieldvalue/fieldvalues.h>
#include <vespa/vespalib/encoding/base64.h>

#include <vespa/log/log.h>
LOG_SETUP(".searchlib.documentfieldnode");

namespace search {
namespace expression {

using namespace vespalib;
using namespace document;

IMPLEMENT_ABSTRACT_EXPRESSIONNODE(DocumentAccessorNode, ExpressionNode);
IMPLEMENT_EXPRESSIONNODE(DocumentFieldNode,    DocumentAccessorNode);
IMPLEMENT_EXPRESSIONNODE(GetYMUMChecksumFunctionNode,           DocumentAccessorNode);
IMPLEMENT_EXPRESSIONNODE(GetDocIdNamespaceSpecificFunctionNode, DocumentAccessorNode);

const vespalib::string DocumentAccessorNode::_S_docId("documentid");

DocumentFieldNode::DocumentFieldNode(const DocumentFieldNode & rhs) :
    DocumentAccessorNode(rhs),
    _fieldPath(rhs._fieldPath),
    _value(rhs._value),
    _fieldName(rhs._fieldName),
    _doc(NULL)
{
}

DocumentFieldNode & DocumentFieldNode::operator = (const DocumentFieldNode & rhs)
{
    if (this != &rhs) {
        DocumentAccessorNode::operator=(rhs);
        _fieldPath = rhs._fieldPath;
        _value = rhs._value;
        _fieldName = rhs._fieldName;
        _doc = NULL;
    }
    return *this;
}

std::unique_ptr<ResultNode>
deduceResultNode(const vespalib::stringref & fieldName, const FieldValue & fv, bool preserveAccurateTypes, bool nestedMultiValue)
{
    std::unique_ptr<ResultNode> value;
    const Identifiable::RuntimeClass & cInfo = fv.getClass();
    if (cInfo.inherits(ByteFieldValue::classId) || cInfo.inherits(IntFieldValue::classId) || cInfo.inherits(LongFieldValue::classId)) {
        if (preserveAccurateTypes) {
            if (cInfo.inherits(ByteFieldValue::classId)) {
                value.reset(nestedMultiValue ? static_cast<ResultNode *>(new Int8ResultNodeVector()) : static_cast<ResultNode *>(new Int8ResultNode()));
            } else if (cInfo.inherits(IntFieldValue::classId)) {
                value.reset(nestedMultiValue ? static_cast<ResultNode *>(new Int32ResultNodeVector()) : static_cast<ResultNode *>(new Int32ResultNode()));
            } else {
                value.reset(nestedMultiValue ? static_cast<ResultNode *>(new Int64ResultNodeVector()) : static_cast<ResultNode *>(new Int64ResultNode()));
            }
        } else {
            value.reset(nestedMultiValue ? static_cast<ResultNode *>(new Int64ResultNodeVector()) : static_cast<ResultNode *>(new Int64ResultNode()));
        }
    } else if (cInfo.inherits(FloatFieldValue::classId) || cInfo.inherits(DoubleFieldValue::classId)) {
        value.reset(nestedMultiValue ? static_cast<ResultNode *>(new FloatResultNodeVector()) : static_cast<ResultNode *>(new FloatResultNode()));
    } else if (cInfo.inherits(StringFieldValue::classId)) {
        value.reset(nestedMultiValue ? static_cast<ResultNode *>(new StringResultNodeVector()) : static_cast<ResultNode *>(new StringResultNode()));
    } else if (cInfo.inherits(RawFieldValue::classId)) {
        value.reset(nestedMultiValue ? static_cast<ResultNode *>(new RawResultNodeVector()) : static_cast<ResultNode *>(new RawResultNode()));
    } else if (cInfo.inherits(CollectionFieldValue::classId) || cInfo.inherits(MapFieldValue::classId)) {
        if (cInfo.inherits(CollectionFieldValue::classId)) {
            value = deduceResultNode(fieldName, *static_cast<const CollectionFieldValue &>(fv).createNested(), preserveAccurateTypes, nestedMultiValue);
        } else if (cInfo.inherits(MapFieldValue::classId)) {
            value = deduceResultNode(fieldName, *static_cast<const MapFieldValue &>(fv).createValue(), preserveAccurateTypes, nestedMultiValue);
        } else {
            throw std::runtime_error(make_string("Can not deduce correct resultclass for documentfield '%s' in based on class '%s'", fieldName.c_str(), cInfo.name()));
        }
        const Identifiable::RuntimeClass & rInfo = value->getClass();
        if (rInfo.inherits(ResultNodeVector::classId)) {
            //Already multivalue, so we are good to go.
        } else if (rInfo.inherits(Int8ResultNode::classId)) {
            value.reset(new Int8ResultNodeVector());
        } else if (rInfo.inherits(Int16ResultNode::classId)) {
            value.reset(new Int16ResultNodeVector());
        } else if (rInfo.inherits(Int32ResultNode::classId)) {
            value.reset(new Int32ResultNodeVector());
        } else if (rInfo.inherits(Int64ResultNode::classId)) {
            value.reset(new Int64ResultNodeVector());
        } else if (rInfo.inherits(FloatResultNode::classId)) {
            value.reset(new FloatResultNodeVector());
        } else if (rInfo.inherits(StringResultNode::classId)) {
            value.reset(new StringResultNodeVector());
        } else if (rInfo.inherits(RawResultNode::classId)) {
            value.reset(new RawResultNodeVector());
        } else {
            throw std::runtime_error(make_string("Can not deduce correct resultclass for documentfield '%s' in based on class '%s'. It nests down to %s which is not expected", fieldName.c_str(), cInfo.name(), rInfo.name()));
        }
    } else {
        throw std::runtime_error(make_string("Can not deduce correct resultclass for documentfield '%s' in based on class '%s'", fieldName.c_str(), cInfo.name()));
    }
    return value;
}

void DocumentFieldNode::onPrepare(bool preserveAccurateTypes)
{
    LOG(debug, "DocumentFieldNode::onPrepare(this=%p)", this);

    if ( !_fieldPath.empty() ) {
        bool nestedMultiValue(false);
        for(document::FieldPath::const_iterator it(_fieldPath.begin()), mt(_fieldPath.end()); !nestedMultiValue && (it != mt); it++) {
            if (it->getType() == document::FieldPathEntry::STRUCT_FIELD) {
                const vespalib::Identifiable::RuntimeClass & cInfo(it->getFieldValueToSet().getClass());
                nestedMultiValue = cInfo.inherits(CollectionFieldValue::classId) || cInfo.inherits(MapFieldValue::classId);
            }
        }
        const document::FieldPathEntry & endOfPath(_fieldPath.back());
        if (endOfPath.getFieldValueToSetPtr() != NULL) {
            const FieldValue& fv = endOfPath.getFieldValueToSet();
            _value.reset(deduceResultNode(_fieldName, fv, preserveAccurateTypes, nestedMultiValue).release());
            if (_value->inherits(ResultNodeVector::classId)) {
                _handler.reset(new MultiHandler(static_cast<ResultNodeVector &>(*_value)));
            } else {
                _handler.reset(new SingleHandler(*_value));
            }
        } else {
            if (endOfPath.getDataType().getClass().inherits(document::StructuredDataType::classId)) {
                throw std::runtime_error(make_string("I am not able to access structured field '%s'", _fieldName.c_str()));
            } else {
                throw std::runtime_error(make_string("I am not able to access field '%s' for reasons I do not know", _fieldName.c_str()));
            }
        }
    }
}

void DocumentFieldNode::onDocType(const DocumentType & docType)
{
    LOG(debug, "DocumentFieldNode::onDocType(this=%p)", this);
    FieldPath::UP path = docType.buildFieldPath(_fieldName);
    if (!path.get() || path->empty()) {
        throw std::runtime_error(make_string("Field %s could not be loacated in documenttype %s", _fieldName.c_str(), docType.getName().c_str()));
    }
    _fieldPath = *path;
}

class FieldValue2ResultNode : public ResultNode
{
public:
    DECLARE_EXPRESSIONNODE(FieldValue2ResultNode);
    FieldValue2ResultNode(const FieldValue * fv=NULL) : _fv(fv) { }
    int64_t onGetInteger(size_t index) const override { (void) index; return _fv ? _fv->getAsLong() : 0; }
    double  onGetFloat(size_t index)   const override { (void) index; return _fv ? _fv->getAsDouble() : 0; }
    ConstBufferRef onGetString(size_t index, BufferRef buf) const override {
        (void) index;
        if (_fv) {
            std::pair<const char*, size_t>  raw = _fv->getAsRaw();
            return ConstBufferRef(raw.first, raw.second);
        }
        return buf;
    }
private:
    virtual void set(const ResultNode&) override;
    virtual size_t hash() const override { return 0; }
    const FieldValue * _fv;
};

char DefaultValue::null = 0;

void DefaultValue::set(const ResultNode&)
{
    throw std::runtime_error("DefaultValue::set(const ResultNode&) is not possible.");
}

void FieldValue2ResultNode::set(const ResultNode&)
{
    throw std::runtime_error("FieldValue2ResultNode::set(const ResultNode&) is not possible.");
}

IMPLEMENT_EXPRESSIONNODE(FieldValue2ResultNode, ResultNode);
IMPLEMENT_EXPRESSIONNODE(DefaultValue, ResultNode);

void DocumentFieldNode::onDoc(const Document & doc)
{
    _doc = & doc;
    _handler->reset();
}

bool DocumentFieldNode::onExecute() const
{
    _doc->iterateNested(_fieldPath.begin(), _fieldPath.end(), *_handler);
    return true;
}

DefaultValue DocumentFieldNode::SingleHandler::_defaultValue;

void
DocumentFieldNode::SingleHandler::onPrimitive(uint32_t, const Content & c)
{
    LOG(spam, "SingleHandler::onPrimitive: field value '%s'", c.getValue().toString().c_str());
    FieldValue2ResultNode converter(&c.getValue());
    _result.set(converter);
}

void
DocumentFieldNode::MultiHandler::onPrimitive(uint32_t, const Content & c)
{
    LOG(spam, "MultiHandler::onPrimitive: field value '%s'", c.getValue().toString().c_str());
    FieldValue2ResultNode converter(&c.getValue());
    _result.push_back_safe(converter);
}

void
DocumentFieldNode::Handler::onCollectionStart(const Content & c)
{
    const document::FieldValue & fv = c.getValue();
    LOG(spam, "onCollectionStart: field value '%s'", fv.toString().c_str());
    if (fv.inherits(document::ArrayFieldValue::classId)) {
        const document::ArrayFieldValue & afv = static_cast<const document::ArrayFieldValue &>(fv);
        LOG(spam, "onCollectionStart: Array size = '%zu'", afv.size());
    } else if (fv.inherits(document::WeightedSetFieldValue::classId)) {
        const document::WeightedSetFieldValue & wsfv = static_cast<const document::WeightedSetFieldValue &>(fv);
        LOG(spam, "onCollectionStart: WeightedSet size = '%zu'", wsfv.size());
    }
}

void
DocumentFieldNode::Handler::onStructStart(const Content & c)
{
    LOG(spam, "onStructStart: field value '%s'", c.getValue().toString().c_str());
}


Serializer & DocumentFieldNode::onSerialize(Serializer & os) const
{
    return os << _fieldName << _value;
}

Deserializer & DocumentFieldNode::onDeserialize(Deserializer & is)
{
    return is >> _fieldName >> _value;
}

void
DocumentFieldNode::visitMembers(vespalib::ObjectVisitor &visitor) const
{
    visit(visitor, "fieldName", _fieldName);
    visit(visitor, "value", _value);
    visitor.openStruct("fieldPath", "FieldPath");
    _fieldPath.visitMembers(visitor);
    visitor.closeStruct();
}

class String2ResultNode : public ResultNode
{
public:
    String2ResultNode(const vespalib::string & s) : _s(s) { }
    virtual int64_t onGetInteger(size_t index) const { (void) index; return strtoul(_s.c_str(), NULL, 0); }
    virtual double  onGetFloat(size_t index)   const { (void) index; return strtod(_s.c_str(), NULL); }
    virtual ConstBufferRef onGetString(size_t index, BufferRef buf) const { (void) index; (void) buf; return ConstBufferRef(_s.c_str(), _s.size()); }
private:
    virtual String2ResultNode * clone() const { return new String2ResultNode(_s); }
    virtual void set(const ResultNode&);
    virtual size_t hash() const { return 0; }
    const vespalib::string & _s;
};

void String2ResultNode::set(const ResultNode&)
{
    throw std::runtime_error("String2ResultNode::set(const ResultNode&) is not possible.");
}

void GetDocIdNamespaceSpecificFunctionNode::onDoc(const Document & doc)
{
    String2ResultNode converter(doc.getId().getScheme().getNamespaceSpecific());
    _value->set(converter);
}

static const FieldBase _G_valueField("value");

Serializer & GetDocIdNamespaceSpecificFunctionNode::onSerialize(Serializer & os) const
{
    return os << _value;
}
Deserializer & GetDocIdNamespaceSpecificFunctionNode::onDeserialize(Deserializer & is)
{
    return is >> _value;
}

void
GetDocIdNamespaceSpecificFunctionNode::visitMembers(vespalib::ObjectVisitor &visitor) const
{
    visit(visitor, _G_valueField.getName(), _value);
}

void GetYMUMChecksumFunctionNode::onDoc(const Document & doc)
{
    const vespalib::string & ymumid = doc.getId().getScheme().getNamespaceSpecific();

    try {
        char decoded[20];
        int len = Base64::decode(ymumid.c_str(), ymumid.size(), decoded, sizeof(decoded));

        if (len != 20) {
            LOG(warning, "Illegal YMUMID '%s' in document id %s. Length(%d) != 20", ymumid.c_str(), doc.getId().toString().c_str(), len);
            _checkSum = 0;
        } else {
            int32_t key[3];
            key[0] = 0;
            memcpy(((char*)key) + 1, decoded + 9, sizeof(key) - 1);
            _checkSum = (key[0] ^ key[1] ^ key[2]);
        }
    } catch (const std::exception & e) {
        LOG(warning, "Illegal YMUMID '%s' in document id %s. Reason : %s", ymumid.c_str(), doc.getId().toString().c_str(), e.what());
        _checkSum = 0;
    }
}

Serializer & GetYMUMChecksumFunctionNode::onSerialize(Serializer & os) const
{
    return _checkSum.serialize(os);
}

Deserializer & GetYMUMChecksumFunctionNode::onDeserialize(Deserializer & is)
{
    return _checkSum.deserialize(is);
}

void
GetYMUMChecksumFunctionNode::visitMembers(vespalib::ObjectVisitor &visitor) const
{
    visit(visitor, "checkSum", _checkSum);
}

}
}

// this function was added by ../../forcelink.sh
void forcelink_file_searchlib_expression_documentfieldnode() {}
