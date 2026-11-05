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
#include <QTextCursor>

#include "gui/UBFloatingPalette.h"

class QButtonGroup;
class QStackedWidget;
class QToolButton;
class UBGraphicsTextItem;

/**
 * Floating palette offering one-tap insertion of maths notation (roots,
 * integrals, sums, limits, fractions, powers, Greek letters, ...) into a
 * text item on the board.
 *
 * Templates are described with a tiny markup language:
 *   ^{...}  superscript         _{...}  subscript
 *   ~{...}  overline            [...]   placeholder (the first one is selected after insertion)
 *   |       caret position      \c      literal character c (for { } [ ] | ^ _ ~ \)
 * The first placeholder or caret is the "slot": a selection in the text item is
 * wrapped into it (e.g. select "x+1", tap the square root).
 */
class UBMathPalette : public UBFloatingPalette
{
    Q_OBJECT

    public:
        explicit UBMathPalette(QWidget* parent = nullptr);
        virtual ~UBMathPalette();

        /** Family of an installed font with good maths coverage, or an empty string. */
        static QString mathFontFamily();

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
        bool eventFilter(QObject* watched, QEvent* event) override;

    private slots:
        void templateButtonClicked();
        void normalTextClicked();
        void pageSelected(int index);

    private:
        struct MathTemplate
        {
            QString label;
            QString toolTip;
            QString markup;
            bool optionalBase; // first placeholder is a base that is omitted if text precedes the cursor
        };

        struct MathPage
        {
            QString title;
            QList<MathTemplate> templates;
        };

        QList<MathPage> buildPages() const;
        QToolButton* createTemplateButton(const MathTemplate& tmpl, QWidget* parent);
        void placeInsideParent();
        void updatePageSize();

        UBGraphicsTextItem* activeTextItem() const;
        UBGraphicsTextItem* createTextItem();
        void insertTemplate(UBGraphicsTextItem* item, const MathTemplate& tmpl);
        void insertMarkup(QTextCursor& cursor, const QString& markup, bool skipBase, const QString& wrapText) const;
        void insertStackedFraction(QTextCursor& cursor, const QColor& color, const QString& wrapText) const;
        void focusTextItem(UBGraphicsTextItem* item);

        QButtonGroup* mPageButtons;
        QStackedWidget* mPages;
        QList<MathTemplate> mTemplates;
        QPointer<UBGraphicsTextItem> mLastTextItem;
        QPoint mPreferredPos;
        bool mHasPreferredPos;
};

#endif // UBMATHPALETTE_H
