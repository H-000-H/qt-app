#include "page.hh"

#include <QBoxLayout>
#include <QHideEvent>
#include <QShowEvent>

namespace app::ui::base
{
    PageBase::PageBase(LayoutMode mode, QWidget *parent)
        : QWidget(parent), layout_mode_(mode)
    {
        root_layout_ = new QBoxLayout(QBoxLayout::LeftToRight, this);
        root_layout_->setContentsMargins(8, 8, 8, 8);
        root_layout_->setSpacing(6);

        primary_nav_        = new QWidget(this);
        primary_nav_layout_ = new QBoxLayout(QBoxLayout::TopToBottom, primary_nav_);
        primary_nav_layout_->setContentsMargins(0, 0, 0, 0);

        workspace_        = new QWidget(this);
        workspace_layout_ = new QBoxLayout(QBoxLayout::TopToBottom, workspace_);
        workspace_layout_->setContentsMargins(0, 0, 0, 0);
        workspace_layout_->setSpacing(6);

        secondary_nav_        = new QWidget(workspace_);
        secondary_nav_layout_ = new QBoxLayout(QBoxLayout::LeftToRight, secondary_nav_);
        secondary_nav_layout_->setContentsMargins(0, 0, 0, 0);

        body_pane_        = new QWidget(workspace_);
        body_pane_layout_ = new QBoxLayout(QBoxLayout::TopToBottom, body_pane_);
        body_pane_layout_->setContentsMargins(0, 0, 0, 0);
        body_pane_layout_->setSpacing(6);

        content_        = new QWidget(body_pane_);
        content_layout_ = new QBoxLayout(QBoxLayout::TopToBottom, content_);
        content_layout_->setContentsMargins(0, 0, 0, 0);

        footer_        = new QWidget(body_pane_);
        footer_layout_ = new QBoxLayout(QBoxLayout::LeftToRight, footer_);
        footer_layout_->setContentsMargins(0, 0, 0, 0);

        /* 挂载顺序在两种模式下相同，切换模式只改 direction */
        workspace_layout_->addWidget(secondary_nav_);
        workspace_layout_->addWidget(body_pane_, 1);
        body_pane_layout_->addWidget(content_, 1);
        body_pane_layout_->addWidget(footer_);
        root_layout_->addWidget(primary_nav_);
        root_layout_->addWidget(workspace_, 1);

        /* 未使用的区域默认隐藏：布局跳过隐藏控件，不占尺寸也不占 spacing，
         * 空间自然由相邻区域（content）占用 */
        primary_nav_->hide();
        secondary_nav_->hide();
        content_->hide();
        footer_->hide();

        rebuild_layout();
    }

    PageBase::~PageBase() = default;

    void PageBase::set_layout_mode(LayoutMode mode)
    {
        if (layout_mode_ == mode)
            return;
        layout_mode_ = mode;
        rebuild_layout();
    }

    void PageBase::rebuild_layout()
    {
        const bool left_nav = (layout_mode_ == LayoutMode::LeftNavTopTabs);

        root_layout_->setDirection(left_nav ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
        primary_nav_layout_->setDirection(left_nav ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        workspace_layout_->setDirection(left_nav ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        secondary_nav_layout_->setDirection(left_nav ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    }

    void PageBase::replace_widget(QWidget *&slot, QBoxLayout *layout, QWidget *w, int stretch, Qt::Alignment alignment)
    {
        QWidget *container = layout->parentWidget();

        if (slot == w)
        {
            if (slot != nullptr)
                layout->setAlignment(slot, alignment);
            return;
        }

        if (slot != nullptr)
        {
            layout->removeWidget(slot);
            slot->deleteLater();
            slot = nullptr;
        }

        if (w == nullptr)
        {
            container->hide(); /* 清空即隐藏：空间让给相邻区域 */
            return;
        }

        if (w->parentWidget() != container)
            w->setParent(container);
        slot = w;
        layout->addWidget(w, stretch, alignment);
        container->show();
    }

    void PageBase::set_primary_nav_widget(QWidget *w, Qt::Alignment alignment)
    {
        replace_widget(primary_nav_widget_, primary_nav_layout_, w, 0, alignment);
    }

    void PageBase::set_secondary_nav_widget(QWidget *w, Qt::Alignment alignment)
    {
        replace_widget(secondary_nav_widget_, secondary_nav_layout_, w, 0, alignment);
    }

    void PageBase::set_content_widget(QWidget *w, Qt::Alignment alignment)
    {
        replace_widget(content_widget_, content_layout_, w, 1, alignment);
    }

    void PageBase::set_footer_widget(QWidget *w, Qt::Alignment alignment)
    {
        replace_widget(footer_widget_, footer_layout_, w, 0, alignment);
    }

    void PageBase::showEvent(QShowEvent *e)
    {
        QWidget::showEvent(e);
        if (!entered_once_)
        {
            entered_once_ = true;
            on_enter();
            emit entered();
        }
    }

    void PageBase::hideEvent(QHideEvent *e)
    {
        QWidget::hideEvent(e);
        on_leave();
        emit left();
    }
}
