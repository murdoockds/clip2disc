#include "clickoverlay.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QMouseEvent>

ClickOverlay::ClickOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet("background-color: black;");

    m_label = new QLabel("Click to select video", this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet(
        "color: white;"
        "font-size: 18px;"
        "font-weight: 500;"
        );

    auto *layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(m_label);
    layout->addStretch();
}

void ClickOverlay::showText(bool visible)
{
    m_label->setVisible(visible);
}

void ClickOverlay::mousePressEvent(QMouseEvent *event)
{
    qDebug() << "Overlay clicked";
    emit clicked();
}
