/****************************************************************************
**
** Copyright (C) 2014-2019 Dinu SV.
** (contact: mail@dinusv.com)
** This file is part of Livekeys Application.
**
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
****************************************************************************/

#include "live/qmleditfragment.h"
#include "live/qmldeclaration.h"
#include "live/codepalette.h"
#include "live/projectdocument.h"
#include "live/projectfile.h"
#include "qmlcodeconverter.h"
#include "qmlbindingchannel.h"
#include "qmlbindingspan.h"
#include "qmlbindingspanmodel.h"

namespace lv{

/**
 * \class lv::QmlEditFragment
 * \ingroup lveditqmljs
 * \brief An editing fragment for a lv::ProjectDocument.
 *
 * An editing fragment represents a section within a lv::ProjectDocument that is connected to
 * the running application. Fragments have palettes associated with them, and can write code
 * based on the given value of a palette. They provide the set of binding channels connected
 * to the application.
 */

/**
 * \brief QmlEditFragment contructor
 *
 * The Fragment is constructed from a \p declaration object and a \p palette object.
 */
QmlEditFragment::QmlEditFragment(QmlDeclaration::Ptr declaration, QObject *parent)
    : QObject(parent)
    , m_declaration(declaration)
    , m_bindingPalette(nullptr)
    , m_bindingSpan(new QmlBindingSpan(this))
    , m_visualParent(nullptr)
    , m_bindingSpanModel(nullptr)
    , m_refCount(0)
{
    if (m_declaration->isForSlot()){
        m_location = Slot;
    } else if (m_declaration->isForImports()){
        m_location = Imports;
    } else if (m_declaration->isForObject()){
        m_location = Object;
    } else if (m_declaration->isForProperty()){
        m_location = Property;
    }
}

/**
 * \brief QmlEditFragment destructor
 */
QmlEditFragment::~QmlEditFragment(){
    for ( auto pal : m_palettes ){
        pal->deleteLater();
    }

    ProjectDocumentSection::Ptr section = declaration()->section();
    ProjectDocument* doc = declaration()->document();
    doc->removeSection(section);

    for ( auto it = childFragments().begin(); it != childFragments().end(); ++it ){
        QmlEditFragment* edit = *it;
        edit->deleteLater();
    }

    delete m_bindingSpanModel;
    delete m_bindingSpan;
}

void QmlEditFragment::setRelativeBinding(const QSharedPointer<QmlBindingPath> &bp){

    QmlBindingPath::Ptr bindingToParent = bp;

    QmlEditFragment* parent = parentFragment();
    if( parent && parent->parentFragment() ){
        bindingToParent = QmlBindingPath::join(parent->m_relativeBinding, bindingToParent);
    }

    m_relativeBinding = bindingToParent;

    QmlEditFragment* root = rootFragment();
    if ( !root )
        return;

    QList<QmlBindingChannel::Ptr> bc = root->bindingSpan()->channels();
    for ( auto it = bc.begin(); it != bc.end(); ++it ){
        QmlBindingChannel::Ptr& bc = *it;
        if ( bc->isEnabled() ){
            QmlBindingPath::Ptr newbp = QmlBindingPath::join(bc->bindingPath(), m_relativeBinding, false);
            Runnable* r = bc->runnable();
            QmlBindingChannel::Ptr newbc = DocumentQmlInfo::traverseBindingPath(newbp, r);
            newbc->setEnabled(true);
            bindingSpan()->addChannel(newbc);
        }
    }
}

/**
 * \brief Returns the lv::QmlDeclaration's value postion
 */
int QmlEditFragment::valuePosition() const{
    return m_declaration->valuePosition();
}


/**
 * \brief Returns the lv::QmlDeclaration's value length
 */
int QmlEditFragment::valueLength() const{
    return m_declaration->valueLength();
}

bool QmlEditFragment::isBuilder() const{
    if ( m_bindingSpan->connectionChannel() )
        return m_bindingSpan->connectionChannel()->isBuilder();
    return false;
}

void QmlEditFragment::rebuild(){
    if ( !isBuilder() || !m_bindingSpan->connectionChannel() )
        return;

    m_bindingSpan->connectionChannel()->rebuild();
}

QString QmlEditFragment::defaultValue() const{
    return QmlTypeInfo::typeDefaultValue(m_declaration->type().name());
}

CodePalette *QmlEditFragment::palette(const QString &type){
    for ( auto it = begin(); it != end(); ++it ){
        CodePalette* current = *it;
        if ( current->type() == type )
            return current;
    }
    return nullptr;
}

void QmlEditFragment::addPalette(CodePalette *palette){
    m_palettes.append(palette);
}

void QmlEditFragment::removePalette(CodePalette *palette){
    for ( auto it = begin(); it != end(); ++it ){
        CodePalette* cp = *it;
        if ( cp == palette ){
            emit aboutToRemovePalette(palette);
            m_palettes.erase(it);
            if ( bindingPalette() != palette )
                palette->deleteLater();
            break;
        }
    }

    if (m_palettes.size() == 0)
    {
        emit paletteListEmpty();
    }
}

int QmlEditFragment::totalPalettes() const{
    return m_palettes.size();
}

void QmlEditFragment::removeBindingPalette(){
    if ( !m_bindingPalette )
        return;

    if ( hasPalette(m_bindingPalette ) )
        m_bindingPalette = nullptr;
    else {
        m_bindingPalette->deleteLater();
        m_bindingPalette = nullptr;
    }
}

void QmlEditFragment::setBindingPalette(CodePalette *palette){
    removeBindingPalette();
    m_bindingPalette = palette;
}

void QmlEditFragment::addChildFragment(QmlEditFragment *edit){
    m_childFragments.append(edit);

}

void QmlEditFragment::removeChildFragment(QmlEditFragment *edit){
    for ( auto it = m_childFragments.begin(); it != m_childFragments.end(); ++it ){

        if ( *it == edit ){
            edit->emitRemoval();

            for ( auto nextIt = it + 1; nextIt != m_childFragments.end(); ++nextIt ){
                QmlEditFragment* nextFrag = *nextIt;
                const QList<QmlBindingChannel::Ptr> &chs = nextFrag->bindingSpan()->channels();

                for ( QmlBindingChannel::Ptr channel : chs ){
                    if ( channel->listIndex() > -1 ){
                        QQmlProperty pp = channel->property();
                        channel->updateConnection(pp, channel->listIndex() - 1);
                    }
                }
            }

            edit->deleteLater();
            m_childFragments.erase(it);
            return;
        }
    }
}

void QmlEditFragment::setObjectId(QString id)
{
    m_objectId = id;
}

QString QmlEditFragment::objectId()
{
    return m_objectId;
}

/**
 * \brief Writes the \p code to the value part of this fragment
 */
void QmlEditFragment::write(const QString &code){
    ProjectDocument* document = m_declaration->document();

    if ( document->editingStateIs(ProjectDocument::Palette))
        return;

    document->addEditingState(ProjectDocument::Palette);

    {
        QTextCursor tc(document->textDocument());
        tc.setPosition(valuePosition());
        tc.setPosition(valuePosition() + valueLength(), QTextCursor::KeepAnchor);
        tc.beginEditBlock();
        tc.removeSelectedText();
        tc.insertText(code);
        tc.endEditBlock();
    }

    document->removeEditingState(ProjectDocument::Palette);
}

QObject *QmlEditFragment::readObject()
{
    QList<QmlBindingChannel::Ptr> channels = bindingSpan()->channels();
    for ( auto it = channels.begin(); it != channels.end(); ++it ){

        const QmlBindingChannel::Ptr& bc = *it;
        if ( bc->isEnabled() ){
            if ( bc->property().propertyTypeCategory() == QQmlProperty::List ){
                QQmlListReference ppref = qvariant_cast<QQmlListReference>(bc->property().read());
                if (ppref.canAt()){
                    QObject* parent = ppref.count() > 0 ? ppref.at(0)->parent() : ppref.object();

                    // create correct order for list reference
                    QObjectList ordered;
                    for (auto child: parent->children())
                    {
                        bool found = false;
                        for (int i = 0; i < ppref.count(); ++i)
                            if (child == ppref.at(i)){
                                found = true;
                                break;
                            }
                        if (found)
                            ordered.push_back(child);
                    }
                    return ordered[bc->listIndex()];
                }
            }
            else if (bc->property().propertyTypeCategory() == QQmlProperty::Object){
                return qvariant_cast<QObject*>(bc->property().read());
            }
        }
    }

    return nullptr;
}

/**
 * \brief Reads the code value of this fragment and returns it.
 */
QString QmlEditFragment::readValueText() const{
    QTextCursor tc(m_declaration->document()->textDocument());
    tc.setPosition(valuePosition());
    tc.setPosition(valuePosition() + valueLength(), QTextCursor::KeepAnchor);
    return tc.selectedText();
}

void QmlEditFragment::updatePaletteValue(CodePalette *palette){
    QmlBindingChannel::Ptr inputChannel = bindingSpan()->connectionChannel();
    if ( !inputChannel )
        return;

    if ( inputChannel->listIndex() == -1 ){
        palette->setValueFromBinding(inputChannel->property().read());
    } else {
        QQmlListReference ppref = qvariant_cast<QQmlListReference>(inputChannel->property().read());
        QObject* parent = ppref.count() > 0 ? ppref.at(0)->parent() : ppref.object();

        // create correct order for list reference
        QObjectList ordered;

        for (auto child: parent->children())
        {
            bool found = false;
            for (int i = 0; i < ppref.count(); ++i)
                if (child == ppref.at(i)){
                    found = true;
                    break;
                }
            if (found)
                ordered.push_back(child);
        }

        palette->setValueFromBinding(QVariant::fromValue(ordered[inputChannel->listIndex()]));
    }
}

QmlEditFragment *QmlEditFragment::parentFragment(){
    return qobject_cast<QmlEditFragment*>(parent());
}

QmlEditFragment *QmlEditFragment::rootFragment(){
    QmlEditFragment* root = parentFragment();
    if ( !root )
        return nullptr;

    while ( root->parentFragment() ){
        root = root->parentFragment();
    }
    return root;
}

void QmlEditFragment::emitRemoval(){
    emit aboutToBeRemoved();

    for ( auto it = begin(); it != end(); ++it ){
        CodePalette* cp = *it;
        cp->deleteLater();
    }

    if ( m_bindingPalette )
        m_bindingPalette->deleteLater();


    m_palettes.clear();
    m_bindingPalette = nullptr;
}

QmlBindingSpanModel* QmlEditFragment::bindingModel(lv::CodeQmlHandler *){
    if ( !m_bindingSpanModel ){
        m_bindingSpanModel = new QmlBindingSpanModel(this);
        QQmlEngine::setObjectOwnership(m_bindingSpanModel, QQmlEngine::CppOwnership);
        connect(m_bindingSpanModel, &QmlBindingSpanModel::inputPathIndexChanged, this, &QmlEditFragment::connectionChanged);
    }
    return m_bindingSpanModel;
}

QString QmlEditFragment::type() const{
    return m_declaration->type().join();
}

QString QmlEditFragment::typeName() const{
    return m_declaration->type().name();
}

QString QmlEditFragment::identifier() const
{
    const QStringList& ic = m_declaration->identifierChain();
    if (ic.size() == 0) return "";
    return ic[ic.size()-1];
}

QList<QObject*> QmlEditFragment::paletteList() const{
    QList<QObject*> result;
    for (CodePalette* palette : m_palettes)
        result.append(palette);
    return result;
}

QList<QObject *> QmlEditFragment::getChildFragments() const{
    QList<QObject*> result;
    for (QmlEditFragment* edit : m_childFragments)
        result.append(edit);
    return result;
}

void QmlEditFragment::updateValue(){
    QmlBindingChannel::Ptr inputPath = bindingSpan()->connectionChannel();

    if ( inputPath && inputPath->listIndex() == -1 ){
        for ( auto it = m_palettes.begin(); it != m_palettes.end(); ++it ){
            CodePalette* cp = *it;
            cp->setValueFromBinding(inputPath->property().read());
        }
        if ( m_bindingPalette ){
            m_bindingPalette->setValueFromBinding(inputPath->property().read());
            QmlCodeConverter* cvt = static_cast<QmlCodeConverter*>(m_bindingPalette->extension());
            cvt->whenBinding().call();
        }
    }
}

void QmlEditFragment::__inputRunnableObjectReady(){
    QmlBindingChannel::Ptr inputChannel = bindingSpan()->connectionChannel();
    if ( inputChannel && inputChannel->listIndex() == -1 ){
        inputChannel->property().connectNotifySignal(this, SLOT(updateValue()));
    }
}

void QmlEditFragment::addNestedObjectInfo(QVariantMap& object)
{
    m_nestedObjectsInfo.push_back(object);
}

void QmlEditFragment::clearNestedObjectsInfo()
{
    m_nestedObjectsInfo = QVariantList();
}

void QmlEditFragment::setObjectInfo(QVariantMap &info)
{
    m_objectInfo = info;
}

void QmlEditFragment::incrementRefCount()
{
    ++m_refCount;
    emit refCountChanged();
}

void QmlEditFragment::decrementRefCount()
{
    --m_refCount;
    emit refCountChanged();
}

int QmlEditFragment::refCount()
{
    return m_refCount;
}

QVariantList QmlEditFragment::nestedObjectsInfo()
{
    return m_nestedObjectsInfo;
}

QVariantMap QmlEditFragment::objectInfo()
{
    return m_objectInfo;
}

void QmlEditFragment::signalPropertyAdded(QmlEditFragment *ef, bool expandDefault)
{
    emit propertyAdded(ef, expandDefault);
}

void QmlEditFragment::signalObjectAdded(QmlEditFragment *ef, QPointF cursorCoords)
{
    emit objectAdded(ef, cursorCoords);
}


}// namespace
