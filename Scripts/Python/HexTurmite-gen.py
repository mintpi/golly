# Generator for Hexagonal Turmite rules

import golly
import random
import string
from glife.EmulateHexagonal import *
from glife.WriteRuleTable import *

prefix = 'HexTurmite' 

turns = [1,2,4,8,16,32]

# http://bytes.com/topic/python/answers/25176-list-subsets
get_subsets = lambda items: [[x for (pos,x) in zip(range(len(items)), items) if (2**pos) & switches] for switches in range(2**len(items))]

# Generate a random rule, while filtering out the dull ones.
# More to try:
# - if turmite can get stuck in period-2 cycles then rule is bad (or might it avoid them?)
# - (extending) if turmite has (c,2 (or 8),s) for state s and color c then will loop on the spot (unlikely to avoid?)
example_spec = '{{{1, 2, 0}, {0, 1, 0}}}'
import random
ns = 2
nc = 3
while True: # (we break out if ok)
    example_spec = '{'
    for state in range(ns):
        if state > 0:
            example_spec += ','
        example_spec += '{'
        for color in range(nc):
            if color > 0:
                example_spec += ','
            new_color = random.randint(0,nc-1)
            dir_to_turn = turns[random.randint(0,len(turns)-1)] # (we don't consider splitting and dying here)
            new_state = random.randint(0,ns-1)
            example_spec += '{' + str(new_color) + "," + str(dir_to_turn) + "," + str(new_state) + '}'
        example_spec += '}'
    example_spec += '}'
    is_rule_acceptable = True
    action_table = eval(string.replace(string.replace(example_spec,'}',']'),'{','['))
    # does Turmite change at least one color?
    changes_one = False
    for state in range(ns):
        for color in range(nc):
            if not action_table[state][color][0] == color:
                changes_one = True
    if not changes_one:
        is_rule_acceptable = False
    # does Turmite write every non-zero color?
    colors_written = set([])
    for state in range(ns):
        for color in range(nc):
            colors_written.add(action_table[state][color][0])
    if not colors_written==set(range(1,nc)):
        is_rule_acceptable = False
    # does Turmite ever turn?
    turmite_turns = False
    for state in range(ns):
        for color in range(nc):
            if not action_table[state][color][1] in [1,32]: # forward, u-turn
                turmite_turns = True
    if not turmite_turns:
        is_rule_acceptable = False
    # does turmite get stuck in any subset of states?
    for subset in get_subsets(range(ns)):
        if len(subset)==0 or len(subset)==ns: # (just an optimisation)
            continue
        leaves_subset = False
        for state in subset:
            for color in range(nc):
                if not action_table[state][color][2] in subset:
                    leaves_subset = True
        if not leaves_subset:
            is_rule_acceptable = False
            break # (just an optimisation)
    # does turmite wobble on the spot? (u-turn that doesn't change color or state)
    for state in range(ns):
        for color in range(nc):
            if action_table[state][color][0]==color and action_table[state][color][1]==32 and action_table[state][color][2]==state:
                is_rule_acceptable = False
    # so was the rule acceptable, in the end?
    if is_rule_acceptable:
        break
        
'''
Some interesting rules, mostly discovered through random search:
1-state 2-color:
In search of Langton's ant:
{{{1,4,0},{0,2,0}}} : with gentle turns we get the same as on the triangular grid (symmetric)
# (by only allowing gentle turns of course a hex grid is the same as a tri grid)
{{{1,16,0},{0,8,0}}} : with sharp turns we get a period-8 glider
{{{1,16,0},{0,2,0}}} : with one sharp and one gentle turn we get a pleasing chaos, with a highway after ~5million steps
{{{1,4,0},{0,8,0}}} : the other way round. You'd think it would be the same but no highway so far (30million steps)
2-state 2-color rules:
{{{1,8,1},{1,1,0}},{{1,8,0},{1,2,0}}} - iceskater makes a thick highway after 40k its
{{{1,2,1},{1,1,0}},{{1,4,1},{1,16,0}}} - loose loopy growth
{{{1,2,0},{1,16,1}},{{1,1,0},{1,1,1}}} - frustrated space filler
{{{1,4,1},{1,1,0}},{{1,1,0},{1,1,1}}} - katana: a 2-speed twin-highway
{{{1,4,0},{1,8,1}},{{1,2,0},{1,16,0}}} - highway after 300k its
3-state 2-color rules:
{{{1,1,2},{1,4,2}},{{1,2,0},{1,1,2}},{{1,32,0},{1,1,1}}} - shuriken iceskater makes a twin-highway after a few false starts
{{{1,16,2},{1,2,2}},{{1,8,0},{1,1,1}},{{1,2,1},{1,4,0}}} - similar
2-state 3-color rules:
{{{2,16,1},{2,16,0},{1,8,1}},{{1,32,1},{2,4,0},{1,2,0}}} - loose growth
{{{1,1,1},{2,32,1},{1,8,1}},{{2,8,0},{2,16,1},{1,1,0}}} - loose geode growth
'''

spec = golly.getstring(
'''This script will create a HexTurmite CA for a given specification.

Enter a specification string: a curly-bracketed table of n_states rows
and n_colors columns, where each entry is a triple of integers. 
The elements of each triple are:
a: the new color of the square
b: the direction(s) for the Turmite to turn (1=Forward, 2=Left, 4=Right, 8=Back-left,
    16=Back-right, 32=U-turn)
c: the new internal state of the Turmite

Example:
{{{1, 4, 0}, {0, 2, 0}}}
Has 1 state and 2 colors. The triple {1,4,0} says: 
1. set the color of the square to 1
2. turn right (4)
3. adopt state 0 and move forward one square
This is the equivalent of Langton's Ant.

Enter string:
''', example_spec, 'Enter HexTurmite specification:')

# convert the specification string into action_table[state][color]
# (convert Mathematica code to Python and run eval)
action_table = eval(string.replace(string.replace(spec,'}',']'),'{','['))
n_states = len(action_table)
n_colors = len(action_table[0])
# (N.B. The terminology 'state' here refers to the internal state of the finite
#       state machine that each Turmite is using, not the contents of each Golly
#       cell. We use the term 'color' to denote the symbol on the 2D 'tape'. The
#       actual 'Golly state' in this emulation of Turmites is given by the 
#       "encoding" section below.)
n_dirs = 6

# TODO: check table is full and valid

total_states = n_colors + n_colors*n_states*n_dirs

# problem if we try to export more than 255 states
if total_states > 255:
    golly.warn("Number of states required exceeds Golly's limit of 255.")
    golly.exit()

# encoding: 
# (0-n_colors: empty square)
def encode(c,s,d):
    # turmite on color c in state s facing away from direction d
    return n_colors + n_dirs*(n_states*c + s) + d

# http://rightfootin.blogspot.com/2006/09/more-on-python-flatten.html
def flatten(l, ltypes=(list, tuple)):
    ltype = type(l)
    l = list(l)
    i = 0
    while i < len(l):
        while isinstance(l[i], ltypes):
            if not l[i]:
                l.pop(i)
                i -= 1
                break
            else:
                l[i:i + 1] = l[i]
        i += 1
    return ltype(l)
    
# convert the string to a form we can embed in a filename
spec_string = '_'.join(map(str,flatten(action_table))) 
# (ambiguous but we have to try something)

# what direction would a turmite have been facing to end up here from direction
# d if it turned t: would_have_been_facing[t][d]
would_have_been_facing={ 
1:[0,1,2,3,4,5], # forward
2:[1,2,3,4,5,0], # left
4:[5,0,1,2,3,4], # right
8:[2,3,4,5,0,1], # back-left
16:[4,5,0,1,2,3], # back-right
32:[3,4,5,0,1,2], # u-turn
}

not_arriving_from_here = [range(n_colors) for i in range(n_dirs)] # (we're going to modify them)
for color in range(n_colors):
    for state in range(n_states):
        turnset = action_table[state][color][1]
        for turn in turns:
            if not turn&turnset: # didn't turn this way
                for dir in range(n_dirs):
                    facing = would_have_been_facing[turn][dir]
                    not_arriving_from_here[dir] += [encode(color,state,facing)]
                    
# What states leave output_color behind?
leaving_color_behind = {}
for output_color in range(n_colors):
    leaving_color_behind[output_color] = [output_color] # (no turmite present)
    for state in range(n_states):
        for color in range(n_colors):
            if action_table[state][color][0]==output_color:
                leaving_color_behind[output_color] += [encode(color,state,d) for d in range(n_dirs)] # any direction

# we can't build the rule tree directly so we collect the transitions ready for emulation
transitions = []

# A single turmite is entering this square:
for s in range(n_states):
    for dir in range(n_dirs):
        # collect all the possibilities for a turmite to arrive in state s from direction dir
        inputs = []
        for state in range(n_states):
            for color in range(n_colors):
                if action_table[state][color][2]==s:
                    turnset = action_table[state][color][1] # sum of all turns
                    inputs += [encode(color,state,would_have_been_facing[turn][dir]) \
                               for turn in turns if turn&turnset]
        if len(inputs)>0:
            for central_color in range(n_colors):
                # output the required transition
                transition = [leaving_color_behind[central_color]] + \
                    [ inputs if i==dir else not_arriving_from_here[i] for i in range(n_dirs) ] + \
                    [ [encode(central_color,s,dir)] ]
                transitions += [transition]

# default: square is left with no turmite present
for output_color,inputs in leaving_color_behind.items():
    transition = [inputs]+[range(total_states)]*n_dirs+[[output_color]]
    transitions += [transition]

rule_name = prefix+'_'+spec_string

# To see the intermediate output as a rule table:
#WriteRuleTable('hexagonal',total_states,transitions,golly.getdir('rules')+rule_name+'_asTable.table')

HexagonalTransitionsToRuleTree('hexagonal',total_states,transitions,rule_name)

# -- make some icons --

palette=[[0,0,0],[0,155,67],[127,0,255],[128,128,128],[185,184,96],[0,100,255],[196,255,254],
    [254,96,255],[126,125,21],[21,126,125],[255,116,116],[116,255,116],[116,116,255],
    [228,227,0],[28,255,27],[255,27,28],[0,228,227],[227,0,228],[27,28,255],[59,59,59],
    [234,195,176],[175,196,255],[171,194,68],[194,68,171],[68,171,194],[72,184,71],[184,71,72],
    [71,72,184],[169,255,188],[252,179,63],[63,252,179],[179,63,252],[80,9,0],[0,80,9],[9,0,80],
    [255,175,250],[199,134,213],[115,100,95],[188,163,0],[0,188,163],[163,0,188],[203,73,0],
    [0,203,73],[73,0,203],[94,189,0],[189,0,94]]

width = 15*(total_states-1)
height = 22
pixels = [[(0,0,0) for x in range(width)] for y in range(height)]

big = [[[0,0,0,1,1,0,0,0,0,0,0,0,0,0,0],
        [0,0,1,1,1,1,1,0,0,0,0,0,0,0,0],
        [0,1,1,1,1,1,1,1,1,0,0,0,0,0,0],
        [1,1,1,1,1,1,1,1,1,1,1,0,0,0,0],
        [1,1,1,1,1,1,1,1,1,1,1,1,0,0,0],
        [0,1,1,1,1,1,1,2,1,1,1,1,0,0,0],
        [0,1,1,1,1,1,1,2,1,1,1,1,1,0,0],
        [0,0,1,1,1,1,1,2,1,1,1,1,1,0,0],
        [0,0,1,1,1,1,1,2,1,1,1,1,1,1,0],
        [0,0,0,1,1,1,1,2,1,1,1,1,1,1,0],
        [0,0,0,1,1,2,1,2,1,2,1,1,1,1,1],
        [0,0,0,0,1,1,2,2,2,1,1,1,1,1,1],
        [0,0,0,0,0,0,1,2,1,1,1,1,1,1,0],
        [0,0,0,0,0,0,0,0,1,1,1,1,1,0,0],
        [0,0,0,0,0,0,0,0,0,0,1,1,0,0,0]],
       [[0,0,0,1,1,0,0,0,0,0,0,0,0,0,0],
        [0,0,1,1,1,1,1,0,0,0,0,0,0,0,0],
        [0,1,1,1,1,1,1,1,1,0,0,0,0,0,0],
        [1,1,1,1,1,1,1,1,1,1,1,0,0,0,0],
        [1,1,1,1,1,1,1,1,1,1,1,1,0,0,0],
        [0,1,1,1,2,1,1,1,1,1,1,1,0,0,0],
        [0,1,1,2,1,1,1,1,1,1,1,1,1,0,0],
        [0,0,2,2,2,2,2,2,2,1,1,1,1,0,0],
        [0,0,1,2,1,1,1,1,1,1,1,1,1,1,0],
        [0,0,0,1,2,1,1,1,1,1,1,1,1,1,0],
        [0,0,0,1,1,1,1,1,1,1,1,1,1,1,1],
        [0,0,0,0,1,1,1,1,1,1,1,1,1,1,1],
        [0,0,0,0,0,0,1,1,1,1,1,1,1,1,0],
        [0,0,0,0,0,0,0,0,1,1,1,1,1,0,0],
        [0,0,0,0,0,0,0,0,0,0,1,1,0,0,0]],
       [[0,0,0,1,1,0,0,0,0,0,0,0,0,0,0],
        [0,0,1,1,1,1,1,0,0,0,0,0,0,0,0],
        [0,1,2,2,2,2,1,1,1,0,0,0,0,0,0],
        [1,1,2,2,1,1,1,1,1,1,1,0,0,0,0],
        [1,1,2,1,2,1,1,1,1,1,1,1,0,0,0],
        [0,1,2,1,1,2,1,1,1,1,1,1,0,0,0],
        [0,1,1,1,1,1,2,1,1,1,1,1,1,0,0],
        [0,0,1,1,1,1,1,2,1,1,1,1,1,0,0],
        [0,0,1,1,1,1,1,1,2,1,1,1,1,1,0],
        [0,0,0,1,1,1,1,1,1,1,1,1,1,1,0],
        [0,0,0,1,1,1,1,1,1,1,1,1,1,1,1],
        [0,0,0,0,1,1,1,1,1,1,1,1,1,1,1],
        [0,0,0,0,0,0,1,1,1,1,1,1,1,1,0],
        [0,0,0,0,0,0,0,0,1,1,1,1,1,0,0],
        [0,0,0,0,0,0,0,0,0,0,1,1,0,0,0]],
       [[0,0,0,1,1,0,0,0,0,0,0,0,0,0,0],
        [0,0,1,1,1,1,1,0,0,0,0,0,0,0,0],
        [0,1,1,1,1,1,1,2,1,0,0,0,0,0,0],
        [1,1,1,1,1,1,2,2,2,1,1,0,0,0,0],
        [1,1,1,1,1,2,1,2,1,2,1,1,0,0,0],
        [0,1,1,1,1,1,1,2,1,1,1,1,0,0,0],
        [0,1,1,1,1,1,1,2,1,1,1,1,1,0,0],
        [0,0,1,1,1,1,1,2,1,1,1,1,1,0,0],
        [0,0,1,1,1,1,1,2,1,1,1,1,1,1,0],
        [0,0,0,1,1,1,1,1,1,1,1,1,1,1,0],
        [0,0,0,1,1,1,1,1,1,1,1,1,1,1,1],
        [0,0,0,0,1,1,1,1,1,1,1,1,1,1,1],
        [0,0,0,0,0,0,1,1,1,1,1,1,1,1,0],
        [0,0,0,0,0,0,0,0,1,1,1,1,1,0,0],
        [0,0,0,0,0,0,0,0,0,0,1,1,0,0,0]],
       [[0,0,0,1,1,0,0,0,0,0,0,0,0,0,0],
        [0,0,1,1,1,1,1,0,0,0,0,0,0,0,0],
        [0,1,1,1,1,1,1,1,1,0,0,0,0,0,0],
        [1,1,1,1,1,1,1,1,1,1,1,0,0,0,0],
        [1,1,1,1,1,1,1,1,1,1,1,1,0,0,0],
        [0,1,1,1,1,1,1,1,1,1,2,1,0,0,0],
        [0,1,1,1,1,1,1,1,1,1,1,2,1,0,0],
        [0,0,1,1,1,1,2,2,2,2,2,2,2,0,0],
        [0,0,1,1,1,1,1,1,1,1,1,2,1,1,0],
        [0,0,0,1,1,1,1,1,1,1,2,1,1,1,0],
        [0,0,0,1,1,1,1,1,1,1,1,1,1,1,1],
        [0,0,0,0,1,1,1,1,1,1,1,1,1,1,1],
        [0,0,0,0,0,0,1,1,1,1,1,1,1,1,0],
        [0,0,0,0,0,0,0,0,1,1,1,1,1,0,0],
        [0,0,0,0,0,0,0,0,0,0,1,1,0,0,0]],
       [[0,0,0,1,1,0,0,0,0,0,0,0,0,0,0],
        [0,0,1,1,1,1,1,0,0,0,0,0,0,0,0],
        [0,1,1,1,1,1,1,1,1,0,0,0,0,0,0],
        [1,1,1,1,1,1,1,1,1,1,1,0,0,0,0],
        [1,1,1,1,1,1,1,1,1,1,1,1,0,0,0],
        [0,1,1,1,1,1,1,1,1,1,1,1,0,0,0],
        [0,1,1,1,1,1,2,1,1,1,1,1,1,0,0],
        [0,0,1,1,1,1,1,2,1,1,1,1,1,0,0],
        [0,0,1,1,1,1,1,1,2,1,1,1,1,1,0],
        [0,0,0,1,1,1,1,1,1,2,1,1,2,1,0],
        [0,0,0,1,1,1,1,1,1,1,2,1,2,1,1],
        [0,0,0,0,1,1,1,1,1,1,1,2,2,1,1],
        [0,0,0,0,0,0,1,1,1,2,2,2,2,1,0],
        [0,0,0,0,0,0,0,0,1,1,1,1,1,0,0],
        [0,0,0,0,0,0,0,0,0,0,1,1,0,0,0]]]
small = [[[0,1,1,0,0,0,0],
          [1,1,1,1,1,0,0],
          [1,1,1,2,1,1,0],
          [0,1,1,2,1,1,0],
          [0,1,2,2,2,1,1],
          [0,0,1,2,1,1,1],
          [0,0,0,0,1,1,0]],
         [[0,1,1,0,0,0,0],
          [1,1,1,1,1,0,0],
          [1,1,2,1,1,1,0],
          [0,2,2,2,2,1,0],
          [0,1,2,1,1,1,1],
          [0,0,1,1,1,1,1],
          [0,0,0,0,1,1,0]],
         [[0,1,1,0,0,0,0],
          [1,2,2,1,1,0,0],
          [1,2,2,1,1,1,0],
          [0,1,1,2,1,1,0],
          [0,1,1,1,2,1,1],
          [0,0,1,1,1,1,1],
          [0,0,0,0,1,1,0]],
         [[0,1,1,0,0,0,0],
          [1,1,1,2,1,0,0],
          [1,1,2,2,2,1,0],
          [0,1,1,2,1,1,0],
          [0,1,1,2,1,1,1],
          [0,0,1,1,1,1,1],
          [0,0,0,0,1,1,0]],
         [[0,1,1,0,0,0,0],
          [1,1,1,1,1,0,0],
          [1,1,1,1,2,1,0],
          [0,1,2,2,2,2,0],
          [0,1,1,1,2,1,1],
          [0,0,1,1,1,1,1],
          [0,0,0,0,1,1,0]],
         [[0,1,1,0,0,0,0],
          [1,1,1,1,1,0,0],
          [1,1,2,1,1,1,0],
          [0,1,1,2,1,1,0],
          [0,1,1,1,2,2,1],
          [0,0,1,1,2,2,1],
          [0,0,0,0,1,1,0]]]

for color in range(n_colors):
    bg = palette[color]
    for row in range(15):
        for column in range(15):
            pixels[row][(color-1)*15+column] = [palette[0],bg,bg][big[0][row][column]]
    for row in range(7):
        for column in range(7):
            pixels[15+row][(color-1)*15+column] = [palette[0],bg,bg][small[0][row][column]]
    for state in range(n_states):
        fg = palette[n_colors+state]
        for dir in range(n_dirs):
            # draw the 15x15 icon
            for row in range(15):
                for column in range(15):
                    pixels[row][(encode(color,state,dir)-1)*15+column] = [palette[0],bg,fg][big[dir][row][column]]
            # draw the 7x7 icon
            for row in range(7):
                for column in range(7):
                    pixels[15+row][(encode(color,state,dir)-1)*15+column] = [palette[0],bg,fg][small[dir][row][column]]
            
WriteBMP( pixels, golly.getdir('rules') + rule_name + ".icons" )

# -- select the new rule --

golly.setalgo('RuleTree')
golly.setrule(rule_name)

golly.new(rule_name+'-demo.rle')
golly.setcell(0,0,encode(0,0,0)) # start with a single turmite

golly.show('Created '+rule_name+'.tree and '+rule_name+'.icons and selected that rule.')

