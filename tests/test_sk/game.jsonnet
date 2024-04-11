//{
// 游戏选项
//options: ['rock', 'paper', 'scissors'],
// 规则定义
/*rules: {
  rock: {
    beats: ['scissors'],
  },
  paper: {
    beats: ['rock'],
  },
  scissors: {
    beats: ['paper'],
  },
},*/
// 生成游戏结果的函数
//function(playerChoice) {
//  player: std.extVar(playerChoice)
//}
/*calculateResult(playerChoice,opponentChoice):: {
    if playerChoice == opponentChoice then "tie"
    else if opponentChoice in self.rules[playerChoice].beats then "win"
    else "lose"
}*/

//}
//{
//  txt: std.extVar('playerChoice'),
//}
//local calc(a) =
//  if 1 == 1 then '1'
//else if opponentChoice in self.rules[playerChoice].beats then "win"
//  else '2';

//{
//  result: calc(std.extVar('playerChoice')),
//}

//!@ 接收前端数据
//!@ --tla-str player="1" --tla-str opponent="2" --tla-str operator="9"


function(playerChoice, opponentChoice) {
  player: if playerChoice == opponentChoice then 'win' else 'lose',
}
