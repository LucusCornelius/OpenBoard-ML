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

#ifndef UBCOLORPALETTEDIALOG_H
#define UBCOLORPALETTEDIALOG_H

#include <QDialog>
#include <QList>

#include "core/UBColorPalette.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;

/**
 * Dialog to manage the named color palettes: create, rename, duplicate,
 * delete and activate palettes and edit their colors.
 */
class UBColorPaletteDialog : public QDialog
{
    Q_OBJECT

    public:
        explicit UBColorPaletteDialog(QWidget* parent = nullptr);

        static void showManager(QWidget* parent);
        static QPushButton* createLauncherButton(QWidget* parent);

    public slots:
        void accept() override;

    private slots:
        void apply();
        void currentPaletteChanged(int row);
        void newPalette();
        void duplicatePalette();
        void renamePalette();
        void deletePalette();
        void activatePalette();
        void addColor();
        void editColor();
        void removeColor();
        void moveColorUp();
        void moveColorDown();
        void resetColors();
        void paletteSizeChanged(int size);

    private:
        UBColorPalette* currentPalette();
        void refreshPaletteList(int selectRow);
        void refreshColors(int selectRow = -1, int selectColumn = -1);
        void updateButtons();
        QStringList paletteNames(int except = -1) const;
        void editCell(int row, int column);

        QList<UBColorPalette> mPalettes;
        int mActiveIndex{0};

        QListWidget* mPaletteList{nullptr};
        QPushButton* mNewButton{nullptr};
        QPushButton* mDuplicateButton{nullptr};
        QPushButton* mRenameButton{nullptr};
        QPushButton* mDeleteButton{nullptr};
        QPushButton* mActivateButton{nullptr};

        QLabel* mColorsTitle{nullptr};
        QTableWidget* mColorTable{nullptr};
        QPushButton* mAddButton{nullptr};
        QPushButton* mEditButton{nullptr};
        QPushButton* mRemoveButton{nullptr};
        QPushButton* mUpButton{nullptr};
        QPushButton* mDownButton{nullptr};
        QPushButton* mResetButton{nullptr};
        QSpinBox* mSizeSpinBox{nullptr};
        QCheckBox* mDeriveCheckBox{nullptr};
};

#endif // UBCOLORPALETTEDIALOG_H
