#ifndef __PROJECTS_WINAUTOMATION_ELEMENT_H_
#define __PROJECTS_WINAUTOMATION_ELEMENT_H_

class Point final : public IPoint {
public:
  Point();
  Point(const PixelType &, const PixelType &);
  ~Point();
  void Release() const override final;

protected:
  const PixelType &GetX() const override final;
  const PixelType &GetY() const override final;

public:
  void operator=(const Point &obj);
  bool operator==(const Point &obj);

private:
  PixelType x = 0;
  PixelType y = 0;
};

class Rect final : public IRect {
public:
  Rect();
  Rect(const RECT &);
  Rect(const PixelType &, const PixelType &, const PixelType &,
       const PixelType &);
  ~Rect();
  void Release() const override final;
  void operator=(const RECT &);
  void operator=(const Rect &);

protected:
  const PixelType &Left() const override final;
  const PixelType &Top() const override final;
  const PixelType &Right() const override final;
  const PixelType &Bottom() const override final;
  PixelType GetWidth() const override final;
  PixelType GetHeight() const override final;

private:
  PixelType left = 0;
  PixelType top = 0;
  PixelType right = 0;
  PixelType bottom = 0;
};

class Position final : public IPosition {
public:
  Position();
  ~Position();
  void Release() const override final;

protected:
  const IPoint *GetPoint() const override final;
  const IRect *GetRect() const override final;

private:
  Rect *rect_ = nullptr;
  Point *point_ = nullptr;
};

class Element final : public IElement {
public:
  Element();
  ~Element();

private:
  void Init();
  void UnInit();

public:
  void Release() const override final;
  const IPoint *GetCaprutePoint() const override final;
  const IPosition *GetPosition() const override final;

private:
  Point *caprute_point_ = nullptr;
  Position *position_ = nullptr;
};
#endif //__PROJECTS_WINAUTOMATION_ELEMENT_H_