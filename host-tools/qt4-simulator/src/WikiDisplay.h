/***************************************************************************
 *   Copyright (C) 2008 by Daniel Mack   *
 *   daniel@caiaq.de   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifndef WIKIDISPLAY_H
#define WIKIDISPLAY_H

#include <QWidget>
#include <QEvent>
#include <QKeyEvent>
#include <QQueue>
#include <QWaitCondition>

class WikiDisplay : public QWidget
{
Q_OBJECT
public:
    WikiDisplay(QWidget *parent = 0);
    ~WikiDisplay();

    void paintEvent(QPaintEvent *);
    QWaitCondition *waitCondition;
    QQueue<QKeyEvent> *keyEventQueue;
    QQueue<QMouseEvent> *mouseEventQueue;
private:
    QByteArray *framebuffer;
protected:
    void keyPressEvent(QKeyEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
};

#endif
