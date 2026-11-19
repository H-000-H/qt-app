#pragma once

#include <QPointer>
#include <QWidget>

class QBoxLayout;
class QShowEvent;
class QHideEvent;

namespace app::ui::base
{
    /**
     * @enum Region
     * @brief 可选创建的页面区域，可按位组合；构造时未启用的区域不会被创建。
     */
    enum class Region : unsigned
    {
        None         = 0u,
        TopBar       = 1u << 0,
        PrimaryNav   = 1u << 1,
        SecondaryNav = 1u << 2,
        Content      = 1u << 3,
        Footer       = 1u << 4,
        All          = TopBar | PrimaryNav | SecondaryNav | Content | Footer
    };

    constexpr Region operator|(Region a, Region b) noexcept
    {
        return static_cast<Region>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }

    constexpr bool has_region(Region set, Region region) noexcept
    {
        return (static_cast<unsigned>(set) & static_cast<unsigned>(region)) != 0u;
    }

    /**
     * @class PageBase
     * @brief 页面基类容器，支持双导航分栏布局的动态切换，同时保留子控件的状态。
     * @note  全部用 QBoxLayout（只改 direction）
     *        支持的布局结构：
     * @code
     *   ┌───────────────────────────────┐   ┌───────────────────────────────┐
     *   │          top_bar_             │   │          top_bar_             │
     *   ├─────────┬─────────────────────┤   ├───────────────────────────────┤
     *   │ primary │  secondary_nav_     │   │         primary_nav_          │
     *   │  _nav_  ├─────────────────────┤   ├─────────┬─────────────────────┤
     *   │         │     content_        │   │ second- │  ary_nav_           │
     *   │         │     content_        │   │         │     content_        │
     *   │         │      footer_        │   │         │      footer_        │
     *   └─────────┴─────────────────────┘   └─────────┴─────────────────────┘
     *   模式 1：左侧主导航+顶部次级标签页      模式 2：顶部主导航+左侧次级菜单
     *
     *   ┌───────────────────────────────┐
     *   │          top_bar_             │
     *   ├─────────────────────┬─────────┤
     *   │  secondary_nav_     │ primary │
     *   ├─────────────────────┤  _nav_  │
     *   │     content_        │         │
     *   │     content_        │         │
     *   │      footer_        │         │
     *   └─────────────────────┴─────────┘
     *   模式 3：左侧工作区+右侧主导航
     *
     *   top_bar_：默认主界面顶栏（界面可选顶栏），不需要时隐藏，主区自动占满
     * @endcode
     */
    class PageBase : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @enum LayoutMode
         * @brief 定义主导航、次级导航与内容区域的排列方式。
         */
        enum class LayoutMode
        {
            LeftNavTopTabs,   /* 左侧主导航侧边栏 + 右侧面板，面板顶部为次级标签页 */
            TopNavLeftSubmenu,/* 顶部主导航栏 + 下方面板，面板左侧为次级菜单 */
            PrimaryRight      /* 右侧主导航侧边栏 + 左侧面板，面板顶部为次级标签页 */
        };

        explicit PageBase(Region regions,
                          LayoutMode mode = LayoutMode::LeftNavTopTabs,
                          QWidget *parent = nullptr);
        explicit PageBase(LayoutMode mode = LayoutMode::LeftNavTopTabs, QWidget *parent = nullptr)
            : PageBase(Region::All, mode, parent) {}
        explicit PageBase(QWidget *parent) : PageBase(Region::All, LayoutMode::LeftNavTopTabs, parent) {}
        ~PageBase() override;

        /**
         * @brief 查询指定区域是否在构造时启用。
         */
        bool has(Region region) const { return has_region(regions_, region); }

        /**
         * @brief 返回构造时启用的区域集合。
         */
        Region regions() const { return regions_; }

        /**
         * @brief 在两种导航布局之间切换；区域内的控件会被保留，不会重建。
         * @param mode 目标布局模式。
         */
        void set_layout_mode(LayoutMode mode);

        /**
         * @brief 返回当前生效的布局模式。
         */
        LayoutMode layout_mode() const { return layout_mode_; }

        /**
         * @name 区域布局槽访问接口
         * @brief 获取各语义区域的布局对象，用于插入自定义控件。
         * @note 取用某个槽时该区域自动显示；一直不取用则区域保持隐藏，布局会跳过隐藏控件
         * @note 只用于 addWidget/insertWidget,方向由set_layout_mode() 统一维护，外部改动会让模式状态失序。
         */
        QBoxLayout *top_bar_slot()
        {
            if (top_bar_ == nullptr)
                return nullptr;
            top_bar_->show();
            return top_bar_layout_;
        }
        QBoxLayout *primary_nav_slot()
        {
            if (primary_nav_ == nullptr)
                return nullptr;
            primary_nav_->show();
            return primary_nav_layout_;
        }
        QBoxLayout *secondary_nav_slot()
        {
            if (secondary_nav_ == nullptr)
                return nullptr;
            secondary_nav_->show();
            return secondary_nav_layout_;
        }
        QBoxLayout *content_slot()
        {
            if (content_ == nullptr)
                return nullptr;
            content_->show();
            return content_layout_;
        }
        QBoxLayout *footer_slot()
        {
            if (footer_ == nullptr)
                return nullptr;
            footer_->show();
            return footer_layout_;
        }
        /** @} */

        /**
         * @name 区域控件设置接口
         * @brief 向每个区域放置一个主控件；重复调用会替换旧控件。
         * @details 例如传递入一个已经包装好的部件 或者传入 一个文字等
         * @note 传入的控件会被重新挂到对应容器下，控件所有权归本页面；
         *       被替换下来的旧控件走 deleteLater()。
         * @note 传 nullptr 表示清空并隐藏该区域，空间由相邻区域自动占用。
         */
        void set_top_bar_widget(QWidget *w, Qt::Alignment alignment = {});
        void set_primary_nav_widget(QWidget *w, Qt::Alignment alignment = {});
        void set_secondary_nav_widget(QWidget *w, Qt::Alignment alignment = {});
        void set_content_widget(QWidget *w, Qt::Alignment alignment = {});
        void set_footer_widget(QWidget *w, Qt::Alignment alignment = {});
        /** @} */

        /**
         * @brief 主动进入页面：显示页面并触发 on_enter()/entered（重复调用不会重复触发）。
         */
        void enter();

        /**
         * @brief 主动离开页面：隐藏页面并触发 on_leave()/left（重复调用不会重复触发）。
         */
        void leave();

    signals:
        /**
         * @brief 每次进入页面时触发。
         */
        void entered();

        /**
         * @brief 每次离开页面时触发（最小化、父窗口隐藏也会触发）。
         */
        void left();

    protected:
        /**
         * @brief 每次进入页面的钩子，子类直接覆写即可，无需自己 connect。
         */
        virtual void on_enter() {}

        /**
         * @brief 离开页面时的钩子。
         */
        virtual void on_leave() {}

        void showEvent(QShowEvent *e) override;
        void hideEvent(QHideEvent *e) override;

    private:
        void rebuild_layout();
        void enter_state();
        void leave_state();
        void replace_widget(QPointer<QWidget> &slot, QBoxLayout *layout, QWidget *w, int stretch, Qt::Alignment alignment);

        LayoutMode layout_mode_ = LayoutMode::LeftNavTopTabs;
        Region regions_ = Region::All;

        /* 结构布局层（切换布局模式时只改方向） */
        QBoxLayout *root_layout_ = nullptr;      /* 外层纵向布局：顶栏在上，主区在下 */
        QWidget *main_pane_ = nullptr;           /* 主区容器，承载主导航与工作区 */
        QBoxLayout *main_layout_ = nullptr;      /* 主区分割布局（左右排列或上下排列） */
        QWidget *workspace_ = nullptr;           /* 工作区容器，承载次级导航、内容区与页脚 */
        QBoxLayout *workspace_layout_ = nullptr; /* workspace_ 内部的分割布局 */
        QWidget *body_pane_ = nullptr;           /* 主体面板容器，承载内容区与页脚 */
        QBoxLayout *body_pane_layout_ = nullptr; /* 垂直堆叠布局，内容区在上、页脚在下 */

        /* 持久化语义区域（布局切换过程中保留内部控件） */
        QWidget *top_bar_ = nullptr;
        QBoxLayout *top_bar_layout_ = nullptr;
        QWidget *primary_nav_ = nullptr;
        QBoxLayout *primary_nav_layout_ = nullptr;
        QWidget *secondary_nav_ = nullptr;
        QBoxLayout *secondary_nav_layout_ = nullptr;
        QWidget *content_ = nullptr;
        QBoxLayout *content_layout_ = nullptr;
        QWidget *footer_ = nullptr;
        QBoxLayout *footer_layout_ = nullptr;

        /* set_*_widget() 当前放置的主控件 */
        QPointer<QWidget> top_bar_widget_;
        QPointer<QWidget> primary_nav_widget_;
        QPointer<QWidget> secondary_nav_widget_;
        QPointer<QWidget> content_widget_;
        QPointer<QWidget> footer_widget_;

        bool entered_ = false;
    };

} // namespace app
