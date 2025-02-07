/*
 * objecttypeseditor.cpp
 * Copyright 2016, Thorbjørn Lindeijer <bjorn@lindeijer.nl>>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "objecttypeseditor.h"
#include "ui_objecttypeseditor.h"

#include "addpropertydialog.h"
#include "custompropertieshelper.h"
#include "object.h"
#include "objecttypesmodel.h"
#include "preferences.h"
#include "properties.h"
#include "session.h"
#include "utils.h"

#include <QtVariantProperty>

#include <QCloseEvent>
#include <QColorDialog>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QScopedValueRollback>
#include <QStyledItemDelegate>
#include <QToolBar>

namespace Tiled {

class ColorDelegate : public QStyledItemDelegate
{
public:
    explicit ColorDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    { }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &,
                   const QModelIndex &) const override;
};

void ColorDelegate::paint(QPainter *painter,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    const QVariant displayData =
            index.model()->data(index, ObjectTypesModel::ColorRole);
    const QColor color = displayData.value<QColor>();
    const QRect rect = option.rect.adjusted(4, 4, -4, -4);

    const QPen linePen(color, 2);
    const QPen shadowPen(Qt::black, 2);

    QColor brushColor = color;
    brushColor.setAlpha(50);
    const QBrush fillBrush(brushColor);

    // Draw the shadow
    painter->setPen(shadowPen);
    painter->setBrush(QBrush());
    painter->drawRect(rect.translated(QPoint(1, 1)));

    painter->setPen(linePen);
    painter->setBrush(fillBrush);
    painter->drawRect(rect);
}

QSize ColorDelegate::sizeHint(const QStyleOptionViewItem &,
                              const QModelIndex &) const
{
    return Utils::dpiScaled(QSize(50, 20));
}


ObjectTypesEditor::ObjectTypesEditor(QWidget *parent)
    : QDialog(parent)
    , mUi(new Ui::ObjectTypesEditor)
    , mObjectTypesModel(new ObjectTypesModel(this))
{
    mUi->setupUi(this);

    mPropertiesHelper = new CustomPropertiesHelper(mUi->propertiesView, this);

    resize(Utils::dpiScaled(size()));

    mUi->objectTypesTable->setModel(mObjectTypesModel);
    mUi->objectTypesTable->setItemDelegateForColumn(1, new ColorDelegate(this));

    QHeaderView *horizontalHeader = mUi->objectTypesTable->horizontalHeader();
    horizontalHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    horizontalHeader->setSectionResizeMode(1, QHeaderView::Fixed);
    horizontalHeader->resizeSection(1, Utils::dpiScaled(50));

    mUi->propertiesView->setResizeMode(QtTreePropertyBrowser::ResizeToContents);
    mUi->propertiesView->setRootIsDecorated(false);
    mUi->propertiesView->setPropertiesWithoutValueMarked(true);

    mAddObjectTypeAction = new QAction(this);
    mRemoveObjectTypeAction = new QAction(this);
    mAddPropertyAction = new QAction(this);
    mRemovePropertyAction = new QAction(this);
    mRenamePropertyAction = new QAction(this);

    mRemoveObjectTypeAction->setEnabled(false);
    mAddPropertyAction->setEnabled(false);
    mRemovePropertyAction->setEnabled(false);
    mRenamePropertyAction->setEnabled(false);

    QIcon addIcon(QLatin1String(":/images/22/add.png"));
    QIcon removeIcon(QLatin1String(":/images/22/remove.png"));

    mAddObjectTypeAction->setIcon(addIcon);
    mRemoveObjectTypeAction->setIcon(removeIcon);
    mAddPropertyAction->setIcon(addIcon);
    mRemovePropertyAction->setIcon(removeIcon);
    mRenamePropertyAction->setIcon(QIcon(QLatin1String(":/images/16/rename.png")));

    Utils::setThemeIcon(mAddObjectTypeAction, "add");
    Utils::setThemeIcon(mRemoveObjectTypeAction, "remove");
    Utils::setThemeIcon(mAddPropertyAction, "add");
    Utils::setThemeIcon(mRemovePropertyAction, "remove");

    auto stretch = new QWidget;
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    QToolBar *objectTypesToolBar = new QToolBar(this);
    objectTypesToolBar->setIconSize(Utils::smallIconSize());
    objectTypesToolBar->addAction(mAddObjectTypeAction);
    objectTypesToolBar->addAction(mRemoveObjectTypeAction);
    objectTypesToolBar->addWidget(stretch);
    objectTypesToolBar->addAction(mUi->actionImport);
    objectTypesToolBar->addAction(mUi->actionExport);

    QToolBar *propertiesToolBar = new QToolBar(this);
    propertiesToolBar->setIconSize(Utils::smallIconSize());
    propertiesToolBar->addAction(mAddPropertyAction);
    propertiesToolBar->addAction(mRemovePropertyAction);
    propertiesToolBar->addAction(mRenamePropertyAction);

    mUi->objectTypesLayout->addWidget(objectTypesToolBar);
    mUi->propertiesLayout->addWidget(propertiesToolBar);

    auto selectionModel = mUi->objectTypesTable->selectionModel();
    connect(selectionModel, &QItemSelectionModel::selectionChanged,
            this, &ObjectTypesEditor::selectedObjectTypesChanged);
    connect(mObjectTypesModel, &ObjectTypesModel::modelReset,
            this, &ObjectTypesEditor::selectFirstType);
    connect(mUi->objectTypesTable, &QAbstractItemView::doubleClicked,
            this, &ObjectTypesEditor::objectTypeIndexClicked);

    connect(mAddObjectTypeAction, &QAction::triggered,
            this, &ObjectTypesEditor::addObjectType);
    connect(mRemoveObjectTypeAction, &QAction::triggered,
            this, &ObjectTypesEditor::removeSelectedObjectTypes);

    connect(mAddPropertyAction, &QAction::triggered,
            this, &ObjectTypesEditor::openAddPropertyDialog);
    connect(mRemovePropertyAction, &QAction::triggered,
            this, &ObjectTypesEditor::removeProperty);
    connect(mRenamePropertyAction, &QAction::triggered,
            this, &ObjectTypesEditor::renameProperty);

    connect(mUi->actionImport, &QAction::triggered,
            this, &ObjectTypesEditor::importObjectTypes);
    connect(mUi->actionExport, &QAction::triggered,
            this, &ObjectTypesEditor::exportObjectTypes);

    connect(mObjectTypesModel, &ObjectTypesModel::dataChanged,
            this, &ObjectTypesEditor::applyObjectTypes);
    connect(mObjectTypesModel, &ObjectTypesModel::rowsInserted,
            this, &ObjectTypesEditor::applyObjectTypes);
    connect(mObjectTypesModel, &ObjectTypesModel::rowsRemoved,
            this, &ObjectTypesEditor::applyObjectTypes);

    connect(mPropertiesHelper, &CustomPropertiesHelper::propertyValueChanged,
            this, &ObjectTypesEditor::propertyValueChanged);

    connect(mUi->propertiesView, &QtTreePropertyBrowser::currentItemChanged,
            this, &ObjectTypesEditor::currentItemChanged);

    mObjectTypesModel->setObjectTypes(Object::objectTypes());

    Preferences *prefs = Preferences::instance();
    connect(prefs, &Preferences::objectTypesChanged, this, &ObjectTypesEditor::objectTypesChanged);

    retranslateUi();
}

ObjectTypesEditor::~ObjectTypesEditor()
{
    delete mUi;
}

void ObjectTypesEditor::closeEvent(QCloseEvent *event)
{
    QWidget::closeEvent(event);
    if (event->isAccepted())
        emit closed();
}

void ObjectTypesEditor::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        mUi->retranslateUi(this);
        retranslateUi();
        break;
    default:
        break;
    }
}

void ObjectTypesEditor::retranslateUi()
{
    mAddObjectTypeAction->setText(tr("Add Object Type"));
    mRemoveObjectTypeAction->setText(tr("Remove Object Type"));

    mAddPropertyAction->setText(tr("Add Property"));
    mRemovePropertyAction->setText(tr("Remove Property"));
    mRenamePropertyAction->setText(tr("Rename Property"));
}

void ObjectTypesEditor::addObjectType()
{
    const QModelIndex newIndex = mObjectTypesModel->addNewObjectType();

    // Select and focus the new row and ensure it is visible
    QItemSelectionModel *sm = mUi->objectTypesTable->selectionModel();
    sm->select(newIndex,
               QItemSelectionModel::ClearAndSelect |
               QItemSelectionModel::Rows);
    sm->setCurrentIndex(newIndex, QItemSelectionModel::Current);
    mUi->objectTypesTable->edit(newIndex);
}

void ObjectTypesEditor::selectedObjectTypesChanged()
{
    const QItemSelectionModel *sm = mUi->objectTypesTable->selectionModel();
    mRemoveObjectTypeAction->setEnabled(sm->hasSelection());

    updateProperties();
}

void ObjectTypesEditor::removeSelectedObjectTypes()
{
    const QItemSelectionModel *sm = mUi->objectTypesTable->selectionModel();
    mObjectTypesModel->removeObjectTypes(sm->selectedRows());
}

void ObjectTypesEditor::objectTypeIndexClicked(const QModelIndex &index)
{
    if (index.column() == 1) {
        QColor color = mObjectTypesModel->objectTypes().at(index.row()).color;
        QColor newColor = QColorDialog::getColor(color, this);
        if (newColor.isValid())
            mObjectTypesModel->setObjectTypeColor(index.row(), newColor);
    }
}

void ObjectTypesEditor::applyObjectTypes()
{
    auto &objectTypes = mObjectTypesModel->objectTypes();

    Preferences *prefs = Preferences::instance();
    mSettingPrefObjectTypes = true;
    prefs->setObjectTypes(objectTypes);
    mSettingPrefObjectTypes = false;

    QString objectTypesFile = prefs->objectTypesFile();
    QDir objectTypesDir = QFileInfo(objectTypesFile).dir();

    if (!objectTypesDir.exists())
        objectTypesDir.mkpath(QLatin1String("."));

    ObjectTypesSerializer serializer;
    if (!serializer.writeObjectTypes(objectTypesFile, objectTypes)) {
        QMessageBox::critical(this, tr("Error Writing Object Types"),
                              tr("Error writing to %1:\n%2")
                              .arg(prefs->objectTypesFile(),
                                   serializer.errorString()));
        return;
    }

    prefs->setObjectTypesFileLastSaved(QFileInfo(objectTypesFile).lastModified());
}

void ObjectTypesEditor::objectTypesChanged()
{
    // ignore signal if ObjectTypesEditor caused it
    if (mSettingPrefObjectTypes)
        return;

    mObjectTypesModel->setObjectTypes(Object::objectTypes());
}

void ObjectTypesEditor::applyPropertyToSelectedTypes(const QString &name, const QVariant &value)
{
    const auto selectionModel = mUi->objectTypesTable->selectionModel();
    const auto selectedRows = selectionModel->selectedRows();

    for (const QModelIndex &index : selectedRows) {
        Properties properties = mObjectTypesModel->objectTypeAt(index).defaultProperties;
        properties.insert(name, value);
        mObjectTypesModel->setObjectTypeProperties(index.row(), properties);
    }

    applyObjectTypes();
}

void ObjectTypesEditor::removePropertyFromSelectedTypes(const QString &name)
{
    const auto selectionModel = mUi->objectTypesTable->selectionModel();
    const auto selectedRows = selectionModel->selectedRows();

    for (const QModelIndex &index : selectedRows) {
        Properties properties = mObjectTypesModel->objectTypeAt(index).defaultProperties;
        properties.remove(name);
        mObjectTypesModel->setObjectTypeProperties(index.row(), properties);
    }

    applyObjectTypes();
}

void ObjectTypesEditor::importObjectTypes()
{
    Session &session = Session::current();
    const QString lastPath = session.lastPath(Session::ObjectTypesFile);
    const QString fileName =
            QFileDialog::getOpenFileName(this, tr("Import Object Types"),
                                         lastPath,
                                         QCoreApplication::translate("File Types", "Object Types files (*.xml *.json)"));
    if (fileName.isEmpty())
        return;

    session.setLastPath(Session::ObjectTypesFile, fileName);

    ObjectTypesSerializer serializer;
    ObjectTypes objectTypes;

    if (serializer.readObjectTypes(fileName, objectTypes)) {
        ObjectTypes currentTypes = mObjectTypesModel->objectTypes();
        for (const ObjectType &type : qAsConst(objectTypes)) {
            auto it = std::find_if(currentTypes.begin(), currentTypes.end(), [&type](const ObjectType &existingType) {
                return existingType.name == type.name;
            });

            if (it != currentTypes.end()) {
                it->color = type.color;
                mergeProperties(it->defaultProperties, type.defaultProperties);
            } else {
                currentTypes.append(type);
            }
        }

        mObjectTypesModel->setObjectTypes(currentTypes);
        applyObjectTypes();
    } else {
        QMessageBox::critical(this, tr("Error Reading Object Types"),
                              serializer.errorString());
    }
}

void ObjectTypesEditor::exportObjectTypes()
{
    Session &session = Session::current();
    QString lastPath = session.lastPath(Session::ObjectTypesFile);

    if (!lastPath.endsWith(QLatin1String(".xml")))
        lastPath.append(QStringLiteral("/objecttypes.xml"));

    const QString fileName =
            QFileDialog::getSaveFileName(this, tr("Export Object Types"),
                                         lastPath,
                                         QCoreApplication::translate("File Types", "Object Types files (*.xml *.json)"));
    if (fileName.isEmpty())
        return;

    session.setLastPath(Session::ObjectTypesFile, fileName);

    ObjectTypesSerializer serializer;
    if (!serializer.writeObjectTypes(fileName, Object::objectTypes())) {
        QMessageBox::critical(this, tr("Error Writing Object Types"),
                              serializer.errorString());
    }
}

void ObjectTypesEditor::updateProperties()
{
    const auto selectionModel = mUi->objectTypesTable->selectionModel();
    const auto selectedRows = selectionModel->selectedRows();

    AggregatedProperties aggregatedProperties;

    for (const QModelIndex &index : selectedRows) {
        ObjectType objectType = mObjectTypesModel->objectTypeAt(index);
        aggregateProperties(aggregatedProperties, objectType.defaultProperties);
    }

    mAddPropertyAction->setEnabled(!selectedRows.isEmpty());

    QScopedValueRollback<bool> updating(mUpdating, true);

    mPropertiesHelper->clear();

    QMapIterator<QString, AggregatedPropertyData> it(aggregatedProperties);
    while (it.hasNext()) {
        it.next();

        const QString &name = it.key();
        const AggregatedPropertyData &data = it.value();

        QtProperty *property = mPropertiesHelper->createProperty(name, data.value());
        mUi->propertiesView->addProperty(property);

        bool everywhere = data.presenceCount() == selectedRows.size();
        bool consistent = everywhere && data.valueConsistent();
        if (!everywhere)
            property->setNameColor(Qt::gray);
        if (!consistent)
            property->setValueColor(Qt::gray);
    }
}

void ObjectTypesEditor::propertyValueChanged(const QString &name,
                                             const QVariant &value)
{
    if (mUpdating)
        return;

    applyPropertyToSelectedTypes(name, value);
}

void ObjectTypesEditor::openAddPropertyDialog()
{
    AddPropertyDialog dialog(window());
    if (dialog.exec() == AddPropertyDialog::Accepted)
        addProperty(dialog.propertyName(), QVariant(dialog.propertyValue()));
}

void ObjectTypesEditor::addProperty(const QString &name, const QVariant &value)
{
    if (name.isEmpty())
        return;

    applyPropertyToSelectedTypes(name, value);
    updateProperties();
    editProperty(name);
}

void ObjectTypesEditor::editProperty(const QString &name)
{
    QtVariantProperty *property = mPropertiesHelper->property(name);
    if (!property)
        return;

    const QList<QtBrowserItem*> propertyItems = mUi->propertiesView->items(property);
    if (!propertyItems.isEmpty())
        mUi->propertiesView->editItem(propertyItems.first());
}

void ObjectTypesEditor::removeProperty()
{
    QtBrowserItem *item = mUi->propertiesView->currentItem();
    if (!item)
        return;

    const QString name = item->property()->propertyName();
    QList<QtBrowserItem *> items = mUi->propertiesView->topLevelItems();
    if (items.count() > 1) {
        int currentItemIndex = items.indexOf(item);
        if (item == items.last()) {
            mUi->propertiesView->setCurrentItem(items.at(currentItemIndex - 1));
        } else {
            mUi->propertiesView->setCurrentItem(items.at(currentItemIndex + 1));
        }
    }

    mPropertiesHelper->deleteProperty(item->property());

    removePropertyFromSelectedTypes(name);
}

void ObjectTypesEditor::renameProperty()
{
    QtBrowserItem *item = mUi->propertiesView->currentItem();
    if (!item)
        return;

    const QString oldName = item->property()->propertyName();

    QInputDialog *dialog = new QInputDialog(mUi->propertiesView);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setInputMode(QInputDialog::TextInput);
    dialog->setLabelText(tr("Name:"));
    dialog->setTextValue(oldName);
    dialog->setWindowTitle(tr("Rename Property"));
    connect(dialog, &QInputDialog::textValueSelected, this, &ObjectTypesEditor::renamePropertyTo);
    dialog->open();
}

void ObjectTypesEditor::renamePropertyTo(const QString &name)
{
    if (name.isEmpty())
        return;

    QtBrowserItem *item = mUi->propertiesView->currentItem();
    if (!item)
        return;

    const QString oldName = item->property()->propertyName();
    if (oldName == name)
        return;

    const auto selectionModel = mUi->objectTypesTable->selectionModel();
    const auto selectedRows = selectionModel->selectedRows();

    for (const QModelIndex &index : selectedRows) {
        Properties properties = mObjectTypesModel->objectTypeAt(index).defaultProperties;
        if (properties.contains(oldName))
            properties.insert(name, properties.take(oldName));
        mObjectTypesModel->setObjectTypeProperties(index.row(), properties);
    }

    applyObjectTypes();
    updateProperties();
}

void ObjectTypesEditor::selectFirstType()
{
    QModelIndex firstIndex = mObjectTypesModel->index(0, 0);
    if (firstIndex.isValid()) {
        mUi->objectTypesTable->selectionModel()->select(firstIndex,
                                                        QItemSelectionModel::ClearAndSelect |
                                                        QItemSelectionModel::Rows);
    } else {
        // make sure the properties view is empty
        updateProperties();
    }
}

void ObjectTypesEditor::currentItemChanged(QtBrowserItem *item)
{
    bool itemSelected = item != nullptr;
    mRemovePropertyAction->setEnabled(itemSelected);
    mRenamePropertyAction->setEnabled(itemSelected);
}

} // namespace Tiled

#include "moc_objecttypeseditor.cpp"
