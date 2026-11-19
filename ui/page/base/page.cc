#include "page.hh"

#include <QBoxLayout>
#include <QHideEvent>
#include <QShowEvent>

namespace app::ui::base
{
    PageBase::PageBase(Region regions, LayoutMode mode, QWidget *parent)
        : QWidget(parent), layout_mode_(mode), regions_(regions)
    {
        root_layout_ = new QBoxLayout(QBoxLayout::TopToBottom, this);
        root_layout_->setContentsMargins(8, 8, 8, 8);
        root_layout_->setSpacing(6);

        if (has(Region::TopBar))
        {
            top_bar_        = new QWidget(this);
            top_bar_layout_ = new QBoxLayout(QBoxLayout::LeftToRight, top_bar_);
            top_bar_layout_->setContentsMargins(0, 0, 0, 0);
        }

        main_pane_        = new QWidget(this);
        main_layout_      = new QBoxLayout(QBoxLayout::LeftToRight, main_pane_);
        main_layout_->setContentsMargins(0, 0, 0, 0);
        main_layout_->setSpacing(6);

        if (has(Region::PrimaryNav))
        {
            primary_nav_        = new QWidget(main_pane_);
            primary_nav_layout_ = new QBoxLayout(QBoxLayout::TopToBottom, primary_nav_);
            primary_nav_layout_->setContentsMargins(0, 0, 0, 0);
        }

        workspace_        = new QWidget(main_pane_);
        workspace_layout_ = new QBoxLayout(QBoxLayout::TopToBottom, workspace_);
        workspace_layout_->setContentsMargins(0, 0, 0, 0);
        workspace_layout_->setSpacing(6);

        if (has(Region::SecondaryNav))
        {
            secondary_nav_        = new QWidget(workspace_);
            secondary_nav_layout_ = new QBoxLayout(QBoxLayout::LeftToRight, secondary_nav_);
            secondary_nav_layout_->setContentsMargins(0, 0, 0, 0);
        }

        body_pane_        = new QWidget(workspace_);
        body_pane_layout_ = new QBoxLayout(QBoxLayout::TopToBottom, body_pane_);
        body_pane_layout_->setContentsMargins(0, 0, 0, 0);
        body_pane_layout_->setSpacing(6);

        if (has(Region::Content))
        {
            content_        = new QWidget(body_pane_);
            content_layout_ = new QBoxLayout(QBoxLayout::TopToBottom, content_);
            content_layout_->setContentsMargins(0, 0, 0, 0);
        }

        if (has(Region::Footer))
        {
            footer_        = new QWidget(body_pane_);
            footer_layout_ = new QBoxLayout(QBoxLayout::LeftToRight, footer_);
            footer_layout_->setContentsMargins(0, 0, 0, 0);
        }

        /* 挂载顺序在两种模式下相同，切换模式只改 direction */
        if (secondary_nav_ != nullptr)
            workspace_layout_->addWidget(secondary_nav_);
        workspace_layout_->addWidget(body_pane_, 1);
        if (content_ != nullptr)
            body_pane_layout_->addWidget(content_, 1);
        if (footer_ != nullptr)
            body_pane_layout_->addWidget(footer_);
        if (primary_nav_ != nullptr)
            main_layout_->addWidget(primary_nav_);
        main_layout_->addWidget(workspace_, 1);
        if (top_bar_ != nullptr)
            root_layout_->addWidget(top_bar_);
        root_layout_->addWidget(main_pane_, 1);

        /* 未使用的区域默认隐藏：布局跳过隐藏控件，不占尺寸也不占 spacing，
         * 空间自然由相邻区域（content）占用 */
        if (top_bar_ != nullptr)
            top_bar_->hide();
        if (primary_nav_ != nullptr)
            primary_nav_->hide();
        if (secondary_nav_ != nullptr)
            secondary_nav_->hide();
        if (content_ != nullptr)
            content_->hide();
        if (footer_ != nullptr)
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
        const bool left_nav      = (layout_mode_ == LayoutMode::LeftNavTopTabs);
        const bool primary_right = (layout_mode_ == LayoutMode::PrimaryRight);
        const bool side_nav      = left_nav || primary_right;

        root_layout_->setDirection(QBoxLayout::TopToBottom);
        main_layout_->setDirection(side_nav ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
        if (primary_nav_layout_ != nullptr)
            primary_nav_layout_->setDirection(side_nav ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        workspace_layout_->setDirection(side_nav ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        if (secondary_nav_layout_ != nullptr)
            secondary_nav_layout_->setDirection(side_nav ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);

        if (primary_nav_ == nullptr)
            return;

        const bool want_primary_first = !primary_right;
        const bool is_primary_first  = main_layout_->indexOf(primary_nav_) < main_layout_->indexOf(workspace_);
        if (want_primary_first == is_primary_first)
            return;

        main_layout_->removeWidget(primary_nav_);
        main_layout_->removeWidget(workspace_);
        if (primary_right)
        {
            main_layout_->addWidget(workspace_, 1);
            main_layout_->addWidget(primary_nav_);
        }
        else
        {
            main_layout_->addWidget(primary_nav_);
            main_layout_->addWidget(workspace_, 1);
        }
    }

    void PageBase::replace_widget(QPointer<QWidget> &slot, QBoxLayout *layout, QWidget *w, int stretch, Qt::Alignment alignment)
    {
        if (layout == nullptr)
            return; /* 区域未启用：忽略，控件仍归调用方所有 */

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

    void PageBase::set_top_bar_widget(QWidget *w, Qt::Alignment alignment)
    {
        replace_widget(top_bar_widget_, top_bar_layout_, w, 0, alignment);
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

    void PageBase::enter()
    {
        show();
        enter_state();
    }

    void PageBase::leave()
    {
        hide();
        leave_state();
    }

    void PageBase::enter_state()
    {
        if (entered_)
            return;
        entered_ = true;
        on_enter();
        emit entered();
    }

    void PageBase::leave_state()
    {
        if (!entered_)
            return;
        entered_ = false;
        on_leave();
        emit left();
    }

    void PageBase::showEvent(QShowEvent *e)
    {
        QWidget::showEvent(e);
        enter_state();
    }

    void PageBase::hideEvent(QHideEvent *e)
    {
        QWidget::hideEvent(e);
        leave_state();
    }
}
