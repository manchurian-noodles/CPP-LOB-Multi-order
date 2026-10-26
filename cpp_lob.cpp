#include <iostream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <cstddef>

class OrderBook {
    public:
        enum class OrderType { Market, Limit, Stop, GoodTillCancelled, FillorKill };
        enum class Side { Buy, Sell };

        class Order {
            public:
                // for 'Stop' orders, 'price' is the trigger price. 
                // stop means stop-market order (default assumption).
                Order (int id, OrderType type, Side side, double price, int quantity)
                    : id(id), type(type), side(side), price(price), quantity(quantity) {}

                int getId () const { return id; }
                OrderType getType () const { return type; }
                Side getSide () const { return side; }
                double getPrice () const { return price; }
                int getQuantity () const { return quantity; }
                void setQuantity (int q) { quantity = q; }
                void setType (OrderType t) { type = t; }

            private:
                int id; OrderType type; Side side; double price; int quantity;
        };

        void addOrder (const Order& order)
        {
            if (order.getQuantity() <= 0) {
                std::cout << "Rejected Order ID: " << order.getId() << " (quantity must be >0)\n";
                return;
            }
            orders.push_back(order); // pushing at the back gives correct time priority
        }

        void cancelOrder (int orderId)
        {
            auto it = std::remove_if(orders.begin(), orders.end(),
                [orderId](const Order& o) { return o.getId() == orderId; });
            if (it != orders.end()) {
                orders.erase(it, orders.end());
                std::cout << "Cancelled Order ID: " << orderId << '\n';
            }
        }

        // each order matches only against orders that arrived before
        // nothing is erased mid-loop: filled orders get quantity 0 
        // and are erased at the end
        void matchOrders ()
        {
            for (std::size_t i = 0; i<orders.size(); i++) {
                Order& o = orders[i];
                if (o.getQuantity() == 0) continue;
                if (o.getType() == OrderType::Stop) {
                    if (!stopTriggered(o)) continue;
                    trigger(o);
                }
                fill(i, i);
                releaseStops(i+1);
            }
            orders.erase(std::remove_if(orders.begin(), orders.end(),
                [](const Order& o) { return o.getQuantity() == 0; }), orders.end());
        }

        void printOrders () const
        {
            for (const auto& order : orders) printOrder(order);
        }

    private:
        std::vector<Order> orders;
        // i need to remember a price for triggering; call it lastPrice
        double lastPrice = 0.0;
        bool hasLast = false;
        static const std::size_t npos = static_cast<std::size_t>(-1);

        // canMatch:= can 'o' trade with resting order 'c'? need for findMatch, liquidity
        // only limit/GTC can rest; only market/FOK/stop are aggressive so not possible with them
        bool canMatch (const Order& o, const Order& c) const
        {
            if (c.getQuantity() <= 0 || c.getSide() == o.getSide()) return false;
            if (c.getType() != OrderType::Limit && c.getType() != OrderType::GoodTillCancelled) return false;
            if (o.getType() == OrderType::Market) return true;   // market order ignores price
            return (o.getSide() == Side::Buy) ? c.getPrice() <= o.getPrice()
                                              : c.getPrice() >= o.getPrice();
        }

        // in cpp, npos is standard notation to mean "no position"/not found.
        // npos is supposed to store max value of size_t. hence i need to use size_t everywhere now
        // best price first, earliest arrival on ties; only looks at indices < bound
        std::size_t findMatch (std::size_t i, std::size_t bound) const
        {
            const Order& o = orders[i];
            std::size_t best = npos;
            for (std::size_t j = 0; j<bound; j++) {
                // we only dynamically update best in this loop, because choosing to trade with best, 
                // is the price-time priority rule. 
                if (j == i || !canMatch(o, orders[j])) continue;
                if (best == npos) { best = j; continue; }
                bool better = (o.getSide() == Side::Buy) ? orders[j].getPrice() < orders[best].getPrice()
                                                         : orders[j].getPrice() > orders[best].getPrice();
                if (better) best = j;
            }
            return best;
        }

        // this liquidity function just computes the maximum quantity I can get to match
        // with an FOK order
        int liquidity (std::size_t i, std::size_t bound) const
        {
            int total = 0;
            for (std::size_t j = 0; j<bound; j++)
                if (j!=i && canMatch(orders[i], orders[j])) total += orders[j].getQuantity();
            return total;
        }

        // we're allowing fill with multiple orders. like a FOK sell of 12 can match with 6+6 buy
        // fill is the actual matching function
        void fill (std::size_t i, std::size_t bound)
        {
            Order& o = orders[i];
            if (o.getType() == OrderType::FillorKill && liquidity(i, bound) < o.getQuantity()) {
                std::cout << "Cancelled FOK OrderID: " << o.getId() << std::endl;
                o.setQuantity(0);
                return;
            }

            // we loop (for getting as many orders to finish the quantity) 
            // and find the best match within each iteration of while loop with findMatch.
            // the fill is min(o.quantity, c.quantity)
            // and we subtract fill from both
            // if its market order, then after all matching any remainder is discarded since market orders
            // never remain in the order book
            // limit and GTC can stay for the future with their remainders.
            while (o.getQuantity() > 0) {
                std::size_t m = findMatch(i, bound);
                if (m == npos) break;
                Order& c = orders[m];
                int qty = std::min(o.getQuantity(), c.getQuantity());
                double px = c.getPrice();   // we say matched order sets the price
                std::cout << "Matched Order ID: " << o.getId() << " with Order ID: " << c.getId()
                          << " at Price: " << std::fixed << std::setprecision(3) << px
                          << " with quantity: " << qty << '\n';
                o.setQuantity(o.getQuantity()-qty);
                c.setQuantity(c.getQuantity()-qty);
                lastPrice = px;
                hasLast = true;
            }
            if (o.getQuantity() > 0 && o.getType() == OrderType::Market) {
                std::cout << "Cancelled unfilled remainder (" << o.getQuantity()
                          << ") of Market Order ID: " << o.getId() << '\n';
                o.setQuantity(0);
            }
            // limit/GTC remainder simply stays in the book
        }

        bool stopTriggered (const Order& s) const
        {
            if (!hasLast) return false;
            return (s.getSide() == Side::Buy) ? lastPrice >= s.getPrice()   // buy stop
                                              : lastPrice <= s.getPrice();  // sell stop
        }

        void trigger (Order& s)
        {
            std::cout << "Stop triggered for Order ID: " << s.getId() << " to Market\n";
            s.setType(OrderType::Market);
        }

        // after trades, wake any dormant stop (among orders that have already arrived); repeat since
        // a triggered stop can trade and move the price again
        void releaseStops (std::size_t bound)
        {
            bool again = true;
            while (again) {
                again = false;
                for (std::size_t j = 0; j<bound; j++) {
                    Order& s = orders[j];
                    if (s.getQuantity() == 0 || s.getType() != OrderType::Stop || !stopTriggered(s)) continue;
                    trigger(s);
                    fill(j, bound);
                    again = true;
                }
            }
        }

        void printOrder (const Order& order) const
        {
            std::cout << "OrderID: " << order.getId()
                      << ", Type: " << static_cast<int>(order.getType())
                      << ", Side: " << (order.getSide() == Side::Buy ? "Buy" : "Sell")
                      << std::fixed << std::setprecision(3) << ", Price: " << order.getPrice()
                      << ", Quantity: " << order.getQuantity() << '\n';
        }
};

int main ()
{
    using OT = OrderBook::OrderType;
    using S = OrderBook::Side;
    OrderBook ob;
    // we assume one type of stock
    ob.addOrder({1,  OT::Limit,             S::Sell, 101.0, 20});
    ob.addOrder({2,  OT::Limit,             S::Sell,  99.0,  5});
    ob.addOrder({3,  OT::GoodTillCancelled, S::Buy,   98.0, 10});
    ob.addOrder({4,  OT::Stop,              S::Buy,  100.0,  6});   // dormant until price >= 100
    ob.addOrder({5,  OT::Market,            S::Buy,    0.0, 10});
    ob.addOrder({6,  OT::FillorKill,        S::Buy,  101.0, 12});   // only 9 available, so killed
    ob.addOrder({7,  OT::FillorKill,        S::Buy,  101.0,  9});   // filled
    ob.addOrder({8,  OT::Limit,             S::Buy,  100.0,  5});
    ob.addOrder({9,  OT::Limit,             S::Sell, 100.0,  8});   // partial fill, remainder rests
    ob.addOrder({10, OT::Stop,              S::Sell, 100.0,  2});   // last trade is 100 so triggers

    std::cout << "Order Book before matching:\n";
    ob.printOrders();
    std::cout << '\n';
    std::cout << "Market log:\n";
    ob.matchOrders();
    std::cout << "\nOrder Book after matching:\n";
    ob.printOrders();
}