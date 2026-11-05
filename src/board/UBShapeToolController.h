/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
 *
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


#ifndef UBSHAPETOOLCONTROLLER_H_
#define UBSHAPETOOLCONTROLLER_H_

#include <QObject>
#include <QList>

class QAction;
class QToolBar;
class UBMainWindow;
class UBToolbarButtonGroup;

/**
 * @brief Manages the toolbar controls of the shapes tool.
 *
 * Shows a button group to choose the kind of shape and a toggle for filling
 * the shape in the board toolbar while the shapes tool is active. The choice
 * is remembered in the settings (Board/ShapeKind, Board/ShapeFill), which are
 * read by the scene when a new shape is drawn.
 */
class UBShapeToolController : public QObject
{
    Q_OBJECT

    public:
        UBShapeToolController(UBMainWindow* mainWindow, QToolBar* toolBar, QAction* insertBefore, QObject* parent = nullptr);
        virtual ~UBShapeToolController();

        static int currentKind();
        static bool fillEnabled();

    private slots:
        void kindActivated(int index);
        void fillToggled(bool checked);
        void stylusToolChanged(int tool, int previousTool);
        void displayText(const QVariant& display);

    private:
        void updateVisibility(int tool);

        UBMainWindow* mMainWindow;
        QList<QAction*> mKindActions;
        UBToolbarButtonGroup* mKindChoice;
        QAction* mKindChoiceAction;
        QAction* mFillAction;
};

#endif /* UBSHAPETOOLCONTROLLER_H_ */
