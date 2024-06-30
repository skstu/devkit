#ifndef CVPP_RANGE_H
#define CVPP_RANGE_H

class Range final : public ICvpp::IRange {
public:
 Range();
 Range(const long& x, const long& y, const long& cx, const long& cy);
 virtual ~Range();
protected:
 void Release() const override final;
 long X() const override final;
 long Y() const override final;
 long CX() const override final;
 long CY() const override final;
private:
 long x_ = 0;
 long y_ = 0;
 long cx_ = 0;
 long cy_ = 0;
};

#endif //CVPP_RANGE_H
