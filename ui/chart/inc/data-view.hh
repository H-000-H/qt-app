/**
 * @brief 本文件模所描述功能纯模板只有.hh一个文件
 */
#pragma once
#include <qabstractitemmodel.h>
#include <qcontainerfwd.h>

#include <qdebug.h>
#include <qhashfunctions.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpointer.h>
#include <qstring.h>
#include <qtableview.h>
#include <qtypes.h>
#include <qvariant.h>
#include <qwidget.h>
#include <concepts>
namespace app
{
    /**
     * @brief ElementData 必须满足的接口：自己描述自己占几列、第 col 列是什么、第 col 列叫什么
     * @note 模板层不做列映射，映射由各结构体负责
     * @note chart_header 必须是 static：表头属于类型而不是某条数据，否则表为空时没有实例可问
     */
    template<typename T>
    concept ChartElement = requires(const T &t, int col)
    {
        { t.chart_column(col) } -> std::convertible_to<QVariant>;
        { T::chart_header(col) } -> std::convertible_to<QString>;
        { T::chart_column_count } -> std::convertible_to<int>;
    };

    template<ChartElement ElementData>
    class Chart : public QAbstractTableModel
    {
    public:
        struct Config
        {
            /**@brief 一条 ElementData 占一行还是占一列 */
            Qt::Orientation orientation = Qt::Orientation::Horizontal;
            /**@brief 导出时字段之间的分隔符 */
            QChar           separator   = QLatin1Char(',');
            /**@brief 视图的父部件，为空则视图自己是顶层窗口；只在第一次 open 时生效 */
            QWidget        *parent      = nullptr;
            /**@brief 顶层窗口的标题；只在第一次 open 时生效 */
            QString         title;
        };

        /**
         * @brief 空构造：只准备模型，视图等上层自己调 open
         */
        Chart(QObject *parent = nullptr) : QAbstractTableModel(parent)
        {

        }

        /**
         * @brief 带配置构造：构造即 open，直接把视图建出来并显示
         */
        Chart(const Config &cfg, QObject *parent = nullptr) : QAbstractTableModel(parent)
        {
            open(cfg);
        }

        ~Chart() override
        {
            if(view_)
                delete view_;
        }

        /**
         * @brief 返回一共有多少行数据。
         */
        int         rowCount(const QModelIndex &parent = {}) const override
        {
            if(parent.isValid())
            {
                return 0;
            }
            return cfg_.orientation == Qt::Orientation::Horizontal
                       ? data_store_.size()
                       : ElementData::chart_column_count;
        }

        /**
         * @brief 返回一共有多少列
         */
        int         columnCount(const QModelIndex &parent = {}) const override
        {
            if(parent.isValid())
            {
                return 0;
            }
            return cfg_.orientation == Qt::Orientation::Horizontal
                       ? ElementData::chart_column_count
                       : data_store_.size();
        }

        /**
         * @brief 解析数据
         * @note 抽象税必须在数据模板里面实现解析
         */
        QVariant data(const QModelIndex &index, int role) const override
        {
            if (role != Qt::DisplayRole || !index.isValid()
                || index.row() < 0 || index.column() < 0)
            {
                return {};
            }

            const int records = data_store_.size();
            const int fields  = ElementData::chart_column_count;
            if(cfg_.orientation == Qt::Orientation::Horizontal)
            {
                if(index.row() >= records || index.column() >= fields)
                    return {};
                return data_store_.at(index.row()).chart_column(index.column());
            }

            if(index.row() >= fields || index.column() >= records)
                return {};
            return data_store_.at(index.column()).chart_column(index.row());
        }


        /**
          *@brief 表头专用，不属于表格单元格 只能放图标 文字
          *@param section：列表 序号从几开始
          *@param o：列表头 行还是列
          *@param role: 同一个单元格，到底用作什么
          *@note 只有和 cfg_.orientation 同轴的一侧是字段名，另一侧留空让视图显示序号
          */
        QVariant    headerData(int section, Qt::Orientation o, int role) const override
        {
            if(role != Qt::DisplayRole || o != cfg_.orientation)
            {
                return {};
            }
            if(section < 0 || section >= ElementData::chart_column_count)
            {
                return {};
            }
            return ElementData::chart_header(section);
        }

        //  采集端写入
        /**
         * @brief 打开视图部件：套用配置，建出表格视图并显示
         * @return false 表示视图没打开（还没构造成功）
         * @note 重复调用只更新配置并复用同一个视图，不会开出第二个窗口；
         *       close/hide 过之后再调本函数会把同一个视图重新显示出来
         * @note cfg.parent / cfg.title 只在第一次打开时生效
         * @note 带 Config 的构造已经在内部调过本函数，只有空构造才需要手动 open
         */
        bool open(const Config &cfg)
        {
            if (cfg_.orientation != cfg.orientation)
            {
                // 表头方向改变，整体重置model
                beginResetModel();
                cfg_ = cfg;
                endResetModel();
            }
            else
                cfg_ = cfg;

            if (!view_)
            {
                view_ = new QTableView(cfg.parent);
                view_->setWindowTitle(cfg.title);
                view_->setModel(this);
                view_->resize(640, 240);
                // 退出时机归上层决定
                view_->setAttribute(Qt::WA_QuitOnClose, false);
            }
            view_->show();
            return is_open();
        }

        /**
         * @brief 关闭视图：窗口收掉，视图对象留着，再 open 复用同一个
         * @return false 表示还没 open 过
         */
        bool close()
        {
            if(!view_)
                return false;
            return view_->close();
        }

        /**
         * @brief 隐藏视图：只是看不见，不发关闭事件
         * @return false 表示还没 open 过
         */
        bool hide()
        {
            if(!view_)
                return false;
            view_->hide();
            return true;
        }

        /**
         * @brief 视图当前是否处于打开状态
         * @note 只看视图自己有没有被 close/hide，挂在父部件下时父部件被藏起来不算
         */
        bool is_open() const
        {
            return view_ && !view_->isHidden();
        }

        /**
         * @brief 各字段名，下标即列号，供导出当表头用
         */
        QStringList headers() const
        {
            QStringList headers;
            headers.reserve(ElementData::chart_column_count);
            for (int col = 0; col < ElementData::chart_column_count; ++col)
            {
                headers << headerData(col, cfg_.orientation, Qt::DisplayRole).toString();
            }
            return headers;
        }


        void write(const ElementData &data_raw)
        {
            const int n = data_store_.size();
            if(cfg_.orientation == Qt::Orientation::Horizontal)
            {
                beginInsertRows({},n, n);
                data_store_.append(data_raw);
                endInsertRows();
            }
            else
            {
                beginInsertColumns({},n,n);
                data_store_.append(data_raw);
                endInsertColumns();
            }

        }
        bool write_batch(const QVector<ElementData> &data_raws)
        {
            if(data_raws.isEmpty())
                return false;
            const int n = data_store_.size();
            if(cfg_.orientation == Qt::Orientation::Horizontal)
            {
                beginInsertRows({},n, n+data_raws.size()-1);
                data_store_.append(data_raws);
                endInsertRows();
            }
            else
            {
                beginInsertColumns({},n,n+data_raws.size()-1);
                data_store_.append(data_raws);
                endInsertColumns();
            }
            return true;
        }
        void clear()
        {
            beginResetModel();
            data_store_.clear();
            endResetModel();
        }

        //  导出
        /**
         * @brief 只取最后写入的那条记录，拼成一行
         * @param[out] line 字段已按 cfg_.separator 分好并转义
         * @retval false 代表表里还没有数据
         * @note 按记录取而不按表行取，所以 Vertical 时它是最右一列的字段，不是最后一行
         */
        bool export_last_line(QString &line) const
        {
            if(data_store_.isEmpty())
                return false;

            const ElementData &latest = data_store_.last();
            QStringList fields;
            fields.reserve(ElementData::chart_column_count);
            for(int col = 0; col < ElementData::chart_column_count; ++col)
            {
                fields << quote(latest.chart_column(col).toString(), cfg_.separator);
            }
            line = fields.join(cfg_.separator);
            return true;
        }

        /**
         * @brief 把表里的数据拼成文本，一行对应表里的一行
         * @param[out] export_data 每行的字段已按 cfg_.separator 分好并转义
         * @retval false 代表根本没有数据
         * @note 不含表头，表头交给 Csv::Config::top_format
         */
        bool export_lines(QStringList &export_data) const
        {
            if(data_store_.isEmpty())
                return false;

            const int rows = rowCount({});
            const int cols = columnCount({});
            QStringList lines;
            lines.reserve(rows);
            for(int r = 0; r < rows; ++r)
            {
                QStringList fields;
                fields.reserve(cols);
                for(int c = 0; c < cols; ++c)
                {
                    fields << quote(data(index(r, c),
                        Qt::DisplayRole).toString(), cfg_.separator);
                }
                lines << fields.join(cfg_.separator);
            }
            export_data = lines;
            return true;
        }

        private:
        /**
         * @brief 字段专用转义：含分隔符、引号或换行时整体加引号，内部的引号双写
         * @param[in] separator 分隔符，跟着 Config 走
         */
        static QString quote(const QString &field, QChar separator)
        {
            if(!field.contains(separator) && !field.contains(QLatin1Char('"'))
               && !field.contains(QLatin1Char('\n')) && !field.contains(QLatin1Char('\r')))
            {
                return field;
            }
            QString quoted = field;
            quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            quoted.prepend(QLatin1Char('"')).append(QLatin1Char('"'));
            return quoted;
        }

        QVector<ElementData>    data_store_;
        Config                  cfg_;
        /**@brief open 建出来的表格视图；用 QPointer 因为父部件可能先把它删掉 */
        QPointer<QTableView>    view_;
    };
}
