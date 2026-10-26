A prerequisites file for the basic financial market terms used, and for clarity of the direction of the project.

Many limitations/assumptions are pointed as comments in the C++ file. My code only logically implements the matching engine 
and each order takes O(n) so total O(n^2), but using std::map<> instead of std::vector<> would bring it down
with O(log n) matching instead of O(n). Using a priority queue (price-time priority) would also optimize our matching.
