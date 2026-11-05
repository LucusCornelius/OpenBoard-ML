/*
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */

#include "UBColorPaletteDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/UBSettings.h"
#include "core/UBSetting.h"
#include "gui/UBColorPickerPopup.h"

#include "core/memcheck.h"

namespace
{
    enum Column
    {
        PenLight = 0,
        PenDark,
        MarkerLight,
        MarkerDark,
        ColumnCount
    };

    bool isDarkColumn(int column)
    {
        return column == PenDark || column == MarkerDark;
    }

    bool isMarkerColumn(int column)
    {
        return column == MarkerLight || column == MarkerDark;
    }

    QList<QColor>& columnColors(UBColorPalette& palette, int column)
    {
        switch (column)
        {
            case PenDark:
                return palette.penDark;
            case MarkerLight:
                return palette.markerLight;
            case MarkerDark:
                return palette.markerDark;
            default:
                return palette.penLight;
        }
    }

    QPixmap swatchPixmap(const QColor& color, bool darkBackground, bool marker)
    {
        QPixmap pixmap(48, 24);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(128, 128, 128), 1));
        painter.setBrush(darkBackground ? QColor(Qt::black) : QColor(Qt::white));
        painter.drawRoundedRect(QRectF(0.5, 0.5, 47, 23), 4, 4);

        QColor fill = color;

        if (marker)
        {
            fill.setAlphaF(UBSettings::settings()->boardMarkerAlpha->get().toDouble());
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(6, 6, 36, 12), 3, 3);

        return pixmap;
    }
}


UBColorPaletteDialog::UBColorPaletteDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Color Palettes"));
    setObjectName(QStringLiteral("colorPaletteDialog"));
    setMinimumSize(900, 480);

    UBColorPaletteManager* manager = UBColorPaletteManager::manager();
    manager->captureFromSettings();
    mPalettes = manager->palettes();
    mActiveIndex = manager->activeIndex();

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(16);
    mainLayout->addLayout(contentLayout, 1);

    // palettes
    QVBoxLayout* paletteLayout = new QVBoxLayout();
    QLabel* paletteTitle = new QLabel(tr("Palettes"), this);
    QFont titleFont = paletteTitle->font();
    titleFont.setBold(true);
    paletteTitle->setFont(titleFont);
    paletteLayout->addWidget(paletteTitle);

    mPaletteList = new QListWidget(this);
    mPaletteList->setMinimumWidth(200);
    paletteLayout->addWidget(mPaletteList, 1);

    QGridLayout* paletteButtons = new QGridLayout();
    mNewButton = new QPushButton(tr("New..."), this);
    mDuplicateButton = new QPushButton(tr("Duplicate"), this);
    mRenameButton = new QPushButton(tr("Rename..."), this);
    mDeleteButton = new QPushButton(tr("Delete"), this);
    mActivateButton = new QPushButton(tr("Set active"), this);
    mActivateButton->setToolTip(tr("Use this palette in the toolbar"));
    paletteButtons->addWidget(mNewButton, 0, 0);
    paletteButtons->addWidget(mDuplicateButton, 0, 1);
    paletteButtons->addWidget(mRenameButton, 1, 0);
    paletteButtons->addWidget(mDeleteButton, 1, 1);
    paletteButtons->addWidget(mActivateButton, 2, 0, 1, 2);
    paletteLayout->addLayout(paletteButtons);
    contentLayout->addLayout(paletteLayout);

    // colors
    QVBoxLayout* colorLayout = new QVBoxLayout();
    mColorsTitle = new QLabel(this);
    mColorsTitle->setFont(titleFont);
    colorLayout->addWidget(mColorsTitle);

    mColorTable = new QTableWidget(0, ColumnCount, this);
    mColorTable->setHorizontalHeaderLabels({
        tr("Pen\n(light background)"),
        tr("Pen\n(dark background)"),
        tr("Marker\n(light background)"),
        tr("Marker\n(dark background)")
    });
    mColorTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mColorTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    mColorTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mColorTable->setIconSize(QSize(48, 24));
    mColorTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mColorTable->verticalHeader()->setDefaultSectionSize(34);
    mColorTable->setToolTip(tr("Double-click a color to edit it"));
    colorLayout->addWidget(mColorTable, 1);

    QHBoxLayout* colorButtons = new QHBoxLayout();
    mAddButton = new QPushButton(tr("Add..."), this);
    mEditButton = new QPushButton(tr("Edit..."), this);
    mRemoveButton = new QPushButton(tr("Remove"), this);
    mUpButton = new QPushButton(tr("Move up"), this);
    mDownButton = new QPushButton(tr("Move down"), this);
    colorButtons->addWidget(mAddButton);
    colorButtons->addWidget(mEditButton);
    colorButtons->addWidget(mRemoveButton);
    colorButtons->addWidget(mUpButton);
    colorButtons->addWidget(mDownButton);
    colorLayout->addLayout(colorButtons);

    QHBoxLayout* optionLayout = new QHBoxLayout();
    QLabel* sizeLabel = new QLabel(tr("Palette size"), this);
    mSizeSpinBox = new QSpinBox(this);
    mSizeSpinBox->setRange(UBColorPalette::minSize(), UBColorPalette::maxSize());
    mSizeSpinBox->setToolTip(tr("Number of colors shown in the toolbar (%1 to %2)")
                             .arg(UBColorPalette::minSize())
                             .arg(UBColorPalette::maxSize()));
    mDeriveCheckBox = new QCheckBox(tr("Derive dark background colors"), this);
    mDeriveCheckBox->setChecked(true);
    mDeriveCheckBox->setToolTip(tr("When a color for light backgrounds is edited, adapt the color for dark backgrounds"));
    mResetButton = new QPushButton(tr("Reset colors"), this);
    mResetButton->setToolTip(tr("Replace the colors of this palette by the default colors"));
    optionLayout->addWidget(sizeLabel);
    optionLayout->addWidget(mSizeSpinBox);
    optionLayout->addSpacing(12);
    optionLayout->addWidget(mDeriveCheckBox, 1);
    optionLayout->addWidget(mResetButton);
    colorLayout->addLayout(optionLayout);

    contentLayout->addLayout(colorLayout, 1);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    mainLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &UBColorPaletteDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &UBColorPaletteDialog::apply);

    connect(mPaletteList, &QListWidget::currentRowChanged, this, &UBColorPaletteDialog::currentPaletteChanged);
    connect(mPaletteList, &QListWidget::itemDoubleClicked, this, &UBColorPaletteDialog::activatePalette);
    connect(mNewButton, &QPushButton::clicked, this, &UBColorPaletteDialog::newPalette);
    connect(mDuplicateButton, &QPushButton::clicked, this, &UBColorPaletteDialog::duplicatePalette);
    connect(mRenameButton, &QPushButton::clicked, this, &UBColorPaletteDialog::renamePalette);
    connect(mDeleteButton, &QPushButton::clicked, this, &UBColorPaletteDialog::deletePalette);
    connect(mActivateButton, &QPushButton::clicked, this, &UBColorPaletteDialog::activatePalette);

    connect(mColorTable, &QTableWidget::cellDoubleClicked, this, &UBColorPaletteDialog::editCell);
    connect(mColorTable, &QTableWidget::currentCellChanged, this, [this]() { updateButtons(); });
    connect(mAddButton, &QPushButton::clicked, this, &UBColorPaletteDialog::addColor);
    connect(mEditButton, &QPushButton::clicked, this, &UBColorPaletteDialog::editColor);
    connect(mRemoveButton, &QPushButton::clicked, this, &UBColorPaletteDialog::removeColor);
    connect(mUpButton, &QPushButton::clicked, this, &UBColorPaletteDialog::moveColorUp);
    connect(mDownButton, &QPushButton::clicked, this, &UBColorPaletteDialog::moveColorDown);
    connect(mResetButton, &QPushButton::clicked, this, &UBColorPaletteDialog::resetColors);
    connect(mSizeSpinBox, SIGNAL(valueChanged(int)), this, SLOT(paletteSizeChanged(int)));

    refreshPaletteList(mActiveIndex);
}


void UBColorPaletteDialog::showManager(QWidget* parent)
{
    UBColorPaletteDialog dialog(parent);
    dialog.exec();
}


QPushButton* UBColorPaletteDialog::createLauncherButton(QWidget* parent)
{
    QPushButton* button = new QPushButton(tr("Color palettes..."), parent);
    button->setToolTip(tr("Manage the named color palettes of pen and marker"));

    connect(button, &QPushButton::clicked, button, [button]() {
        showManager(button->window());
    });

    return button;
}


void UBColorPaletteDialog::accept()
{
    apply();
    QDialog::accept();
}


void UBColorPaletteDialog::apply()
{
    UBColorPaletteManager::manager()->setPalettes(mPalettes, mActiveIndex);

    // the manager normalizes names and sizes
    const int row = mPaletteList->currentRow();
    mPalettes = UBColorPaletteManager::manager()->palettes();
    mActiveIndex = UBColorPaletteManager::manager()->activeIndex();
    refreshPaletteList(row);
}


UBColorPalette* UBColorPaletteDialog::currentPalette()
{
    const int row = mPaletteList->currentRow();

    if (row < 0 || row >= mPalettes.size())
        return nullptr;

    return &mPalettes[row];
}


QStringList UBColorPaletteDialog::paletteNames(int except) const
{
    QStringList names;

    for (int i = 0; i < mPalettes.size(); ++i)
    {
        if (i != except)
            names << mPalettes.at(i).name;
    }

    return names;
}


void UBColorPaletteDialog::refreshPaletteList(int selectRow)
{
    {
        const QSignalBlocker blocker(mPaletteList);
        mPaletteList->clear();

        for (int i = 0; i < mPalettes.size(); ++i)
        {
            QListWidgetItem* item = new QListWidgetItem(mPaletteList);

            if (i == mActiveIndex)
            {
                item->setText(tr("%1 (active)").arg(mPalettes.at(i).name));
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
            }
            else
            {
                item->setText(mPalettes.at(i).name);
            }
        }

        mPaletteList->setCurrentRow(qBound(0, selectRow, int(mPalettes.size()) - 1));
    }

    currentPaletteChanged(mPaletteList->currentRow());
}


void UBColorPaletteDialog::currentPaletteChanged(int row)
{
    Q_UNUSED(row);
    refreshColors(0, PenLight);
}


void UBColorPaletteDialog::refreshColors(int selectRow, int selectColumn)
{
    UBColorPalette* palette = currentPalette();

    const QSignalBlocker blocker(mColorTable);
    mColorTable->clearContents();
    mColorTable->setRowCount(palette ? palette->size() : 0);

    if (palette)
    {
        mColorsTitle->setText(tr("Colors of \"%1\"").arg(palette->name));

        for (int row = 0; row < palette->size(); ++row)
        {
            for (int column = 0; column < ColumnCount; ++column)
            {
                const QColor color = columnColors(*palette, column).value(row);
                QTableWidgetItem* item = new QTableWidgetItem();
                item->setIcon(QIcon(swatchPixmap(color, isDarkColumn(column), isMarkerColumn(column))));
                item->setText(color.name(QColor::HexRgb).toUpper());
                item->setToolTip(color.name(QColor::HexRgb).toUpper());
                mColorTable->setItem(row, column, item);
            }
        }

        const QSignalBlocker sizeBlocker(mSizeSpinBox);
        mSizeSpinBox->setValue(palette->size());

        if (selectRow >= 0 && palette->size() > 0)
        {
            mColorTable->setCurrentCell(qBound(0, selectRow, palette->size() - 1), qBound(0, selectColumn, ColumnCount - 1));
        }
    }
    else
    {
        mColorsTitle->setText(tr("Colors"));
    }

    updateButtons();
}


void UBColorPaletteDialog::updateButtons()
{
    UBColorPalette* palette = currentPalette();
    const int row = mColorTable->currentRow();
    const bool hasPalette = palette != nullptr;
    const bool hasColor = hasPalette && row >= 0 && row < palette->size();

    mDuplicateButton->setEnabled(hasPalette);
    mRenameButton->setEnabled(hasPalette);
    mDeleteButton->setEnabled(hasPalette && mPalettes.size() > 1);
    mActivateButton->setEnabled(hasPalette && mPaletteList->currentRow() != mActiveIndex);

    mAddButton->setEnabled(hasPalette && palette->size() < UBColorPalette::maxSize());
    mEditButton->setEnabled(hasColor);
    mRemoveButton->setEnabled(hasColor && palette->size() > UBColorPalette::minSize());
    mUpButton->setEnabled(hasColor && row > 0);
    mDownButton->setEnabled(hasColor && row < palette->size() - 1);
    mResetButton->setEnabled(hasPalette);
    mSizeSpinBox->setEnabled(hasPalette);
}


void UBColorPaletteDialog::newPalette()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Palette"), tr("Name of the new palette:"),
                                               QLineEdit::Normal, tr("My palette"), &ok);

    if (!ok)
        return;

    UBColorPalette palette = UBColorPalette::builtInDefault();
    palette.name = UBColorPaletteManager::uniqueName(name, paletteNames());
    mPalettes << palette;
    refreshPaletteList(mPalettes.size() - 1);
}


void UBColorPaletteDialog::duplicatePalette()
{
    UBColorPalette* palette = currentPalette();

    if (!palette)
        return;

    UBColorPalette copy = *palette;
    copy.name = UBColorPaletteManager::uniqueName(tr("%1 copy").arg(palette->name), paletteNames());
    mPalettes << copy;
    refreshPaletteList(mPalettes.size() - 1);
}


void UBColorPaletteDialog::renamePalette()
{
    UBColorPalette* palette = currentPalette();

    if (!palette)
        return;

    const int row = mPaletteList->currentRow();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Palette"), tr("New name:"),
                                               QLineEdit::Normal, palette->name, &ok);

    if (!ok || name.trimmed().isEmpty())
        return;

    palette->name = UBColorPaletteManager::uniqueName(name, paletteNames(row));
    refreshPaletteList(row);
}


void UBColorPaletteDialog::deletePalette()
{
    UBColorPalette* palette = currentPalette();

    if (!palette || mPalettes.size() <= 1)
        return;

    const int row = mPaletteList->currentRow();

    if (QMessageBox::question(this, tr("Delete Palette"), tr("Delete the palette \"%1\"?").arg(palette->name))
            != QMessageBox::Yes)
    {
        return;
    }

    mPalettes.removeAt(row);

    if (mActiveIndex == row)
    {
        mActiveIndex = 0;
    }
    else if (mActiveIndex > row)
    {
        --mActiveIndex;
    }

    refreshPaletteList(qMin(row, int(mPalettes.size()) - 1));
}


void UBColorPaletteDialog::activatePalette()
{
    const int row = mPaletteList->currentRow();

    if (row < 0 || row >= mPalettes.size())
        return;

    mActiveIndex = row;
    refreshPaletteList(row);
}


void UBColorPaletteDialog::addColor()
{
    UBColorPalette* palette = currentPalette();

    if (!palette || palette->size() >= UBColorPalette::maxSize())
        return;

    const QColor initial = palette->penLight.isEmpty() ? QColor(Qt::black) : palette->penLight.last();
    const QColor color = UBColorPickerDialog::getColor(initial, this, tr("Add Color"));

    if (!color.isValid())
        return;

    palette->appendColor(color, UBColorPalette::darkVariant(color));
    refreshColors(palette->size() - 1, PenLight);
}


void UBColorPaletteDialog::editColor()
{
    int row = mColorTable->currentRow();
    int column = mColorTable->currentColumn();

    if (row < 0)
        row = 0;

    if (column < 0)
        column = PenLight;

    editCell(row, column);
}


void UBColorPaletteDialog::editCell(int row, int column)
{
    UBColorPalette* palette = currentPalette();

    if (!palette || row < 0 || row >= palette->size() || column < 0 || column >= ColumnCount)
        return;

    QList<QColor>& colors = columnColors(*palette, column);
    const bool dark = isDarkColumn(column);
    const bool marker = isMarkerColumn(column);
    const qreal alpha = marker ? UBSettings::settings()->boardMarkerAlpha->get().toDouble() : 1.0;

    const QColor color = UBColorPickerDialog::getColor(colors.at(row), this,
                                                       mColorTable->horizontalHeaderItem(column)->text().replace('\n', ' '),
                                                       dark ? QColor(Qt::black) : QColor(Qt::white),
                                                       alpha);

    if (!color.isValid())
        return;

    colors[row] = UBColorPalette::opaque(color);

    if (!dark && mDeriveCheckBox->isChecked())
    {
        columnColors(*palette, column + 1)[row] = UBColorPalette::darkVariant(color);
    }

    refreshColors(row, column);
}


void UBColorPaletteDialog::removeColor()
{
    UBColorPalette* palette = currentPalette();
    const int row = mColorTable->currentRow();

    if (!palette || row < 0 || row >= palette->size())
        return;

    if (palette->size() <= UBColorPalette::minSize())
    {
        QMessageBox::information(this, tr("Remove Color"),
                                 tr("A palette needs at least %1 colors.").arg(UBColorPalette::minSize()));
        return;
    }

    palette->removeSlot(row);
    refreshColors(qMin(row, palette->size() - 1), mColorTable->currentColumn());
}


void UBColorPaletteDialog::moveColorUp()
{
    UBColorPalette* palette = currentPalette();
    const int row = mColorTable->currentRow();
    const int column = mColorTable->currentColumn();

    if (!palette || row <= 0)
        return;

    palette->moveSlot(row, row - 1);
    refreshColors(row - 1, column);
}


void UBColorPaletteDialog::moveColorDown()
{
    UBColorPalette* palette = currentPalette();
    const int row = mColorTable->currentRow();
    const int column = mColorTable->currentColumn();

    if (!palette || row < 0 || row >= palette->size() - 1)
        return;

    palette->moveSlot(row, row + 1);
    refreshColors(row + 1, column);
}


void UBColorPaletteDialog::resetColors()
{
    UBColorPalette* palette = currentPalette();

    if (!palette)
        return;

    if (QMessageBox::question(this, tr("Reset Colors"),
                              tr("Replace the colors of \"%1\" by the default colors?").arg(palette->name))
            != QMessageBox::Yes)
    {
        return;
    }

    const QString name = palette->name;
    *palette = UBColorPalette::builtInDefault();
    palette->name = name;
    refreshColors(0, PenLight);
}


void UBColorPaletteDialog::paletteSizeChanged(int size)
{
    UBColorPalette* palette = currentPalette();

    if (!palette || size == palette->size())
        return;

    palette->resize(size, UBColorPalette::builtInDefault());
    refreshColors(mColorTable->currentRow(), mColorTable->currentColumn());
}
