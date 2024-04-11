import random

user_choice = int(input("请选择（0石头/1剪刀/2布）："))
computer_choice = random.randint(0, 2)

print("你的选择：")

if user_choice == 0:
    print("石头")
else:
    if user_choice == 1:
        print("剪刀")
    else:
        print("布")
print("计算机的选择：")
if computer_choice == 0:
    print("石头")
else:
    if computer_choice == 1:
        print("剪刀")
    else:
        print("布")

if user_choice == computer_choice:
    print("平局！")
else:
    if (user_choice == 0 and computer_choice == 1) or \
        (user_choice == 1 and computer_choice == 2) or \
            (user_choice == 2 and computer_choice == 0):
        print("你赢了！")
    else:
        print("计算机赢了！")
