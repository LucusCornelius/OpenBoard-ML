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

#ifndef UBMATHPALETTE_H
#define UBMATHPALETTE_H

#include <QPointer>

#include "domain/UBMathInkLayout.h"
#include "gui/UBFloatingPalette.h"

class QButtonGroup;
class QLabel;
class QLineEdit;
class QSlider;
class QStackedWidget;
class QToolButton;
class UBInkWriter;

/**
 * Floating palette that writes maths notation on the board as handwriting:
 * tapping a symbol (root, integral, sum, limit, fraction, Greek letter, ...) and then
 * the board draws it there with pen strokes in the current pen colour and width.
 * The "ink text" field writes a whole formula typed with a simple syntax
 * (see UBMathInkLayout), e.g. f(x) = x^2 + 1/(x-1).
 */
class UBMathPalette : public UBFloatingPalette
{
    Q_OBJECT

    public:
        explicit UBMathPalette(QWidget* parent = nullptr);
        virtual ~UBMathPalette();

        /** Render a formula as it will be written, for button icons. */
        static QPixmap formulaPixmap(const UBMathInkLayout::Formula& formula, const QSize& size,
                                     const QColor& color, qreal devicePixelRatio);

    public slots:
        /** Show or hide the palette and remember the state in the settings. */
        void setPaletteVisible(bool visible);

    signals:
        /** Emitted when the palette is closed using its own close button. */
        void closed();

    protected:
        void onCloseButtonClicked() override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;

    private slots:
        void templateButtonClicked();
        void inkTextEntered();
        void sizeChanged(int halfCentimetres);
        void pageSelected(int index);
        void placementStarted();
        void placementFinished(bool written);

    private:
        struct MathTemplate
        {
            QString source;
            QString toolTip;
        };

        struct MathPage
        {
            QString title;
            QList<MathTemplate> templates;
        };

        static QList<MathPage> buildPages();
        QToolButton* createTemplateButton(const MathTemplate& tmpl, QWidget* parent);
        void placeInsideParent();
        void updatePageSize();
        void showHint(const QString& text = QString());

        QButtonGroup* mPageButtons{nullptr};
        QStackedWidget* mPages{nullptr};
        QSlider* mSizeSlider{nullptr};
        QLabel* mSizeLabel{nullptr};
        QLineEdit* mInkText{nullptr};
        QLabel* mHint{nullptr};
        UBInkWriter* mWriter{nullptr};
        QList<UBMathInkLayout::Formula> mFormulas;
        QPointer<QToolButton> mActiveButton;
        QPoint mPreferredPos;
        bool mHasPreferredPos{false};
};

#endif // UBMATHPALETTE_H
