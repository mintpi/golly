-- This script runs the current pattern and plays different sounds
-- depending on how the population changes.

local g = golly()
if g.empty() then g.exit("There is no pattern.") end

-- eventually replace these sounds with a larger set of (organ/piano?) notes!!!
local soundsdir = g.getdir("app").."Scripts/Lua/sounds/breakout/"
local sounds = {
    soundsdir.."brick1.ogg",
    soundsdir.."brick2.ogg",
    soundsdir.."brick3.ogg",
    soundsdir.."brick4.ogg",
    soundsdir.."brick5.ogg",
    soundsdir.."brick6.ogg"
}
local volume = 0.2
local minpop = math.maxinteger
local maxpop = math.mininteger
local running = true
local genspersec = 10
local nextgen = 0

--------------------------------------------------------------------------------

function ShowHelp()
    g.note([[
Hit up/down arrows to change volume.
Hit -/+ keys to change gens per sec.
Hit enter/return to toggle generating.
Hit space bar to step 1 gen.
Hit H to see this help.
Hit escape to exit script.
]])
end

--------------------------------------------------------------------------------

function ShowInfo()
    g.show(string.format("gens/sec=%d volume=%.1f (hit H for help)", genspersec, volume))
end

--------------------------------------------------------------------------------

function EventLoop()
    local prevsound, samecount = 0, 0 -- used to detect a repeating sound
    while true do
        local space = false
        local event = g.getevent()
        if #event == 0 then
            g.sleep(5) -- don't hog CPU if idle
        elseif event == "key space none" then
            running = false
            space = true
            nextgen = 0
        elseif event == "key return none" then
            running = not running
        elseif event == "key h none" then
            ShowHelp()
        elseif event == "key up none" then
            volume = math.min(1.0, volume+0.1)
            ShowInfo()
        elseif event == "key down none" then
            volume = math.max(0.0, volume-0.1)
            ShowInfo()
        elseif event == "key = none" or event == "key + none" then
            genspersec = math.min(30, genspersec+1)
            ShowInfo()
        elseif event == "key - none" then
            genspersec = math.max(1, genspersec-1)
            ShowInfo()
        else
            g.doevent(event) -- might be a keyboard shortcut
        end
        if (running or space) and g.millisecs() >= nextgen then
            -- get current population and update minpop and maxpop
            local currpop = tonumber(g.getpop())
            if currpop < minpop then minpop = currpop end
            if currpop > maxpop then maxpop = currpop end
            local poprange = maxpop - minpop
            if poprange == 0 then
                g.sound("play", sounds[#sounds//2], volume)
            else
                local p = (currpop-minpop) / poprange
                -- p is from 0.0 to 1.0
                local i = 1 + math.floor(p * (#sounds-1))
                g.sound("play", sounds[i], volume)
                -- note that we can end up repeating the same sound
                -- (eg. if the initial pattern is a dense Life soup),
                -- so if that happens we reset minpop and maxpop
                if i == prevsound then
                    samecount = samecount + 1
                    if samecount == #sounds then
                        minpop = math.maxinteger
                        maxpop = math.mininteger
                        prevsound, samecount = 0, 0
                    end
                else
                    prevsound = i
                    samecount = 0
                end
            end
            g.run(1)
            g.update()
            if g.empty() then g.exit("The pattern died.") end
            if not space then
                nextgen = g.millisecs() + 1000/genspersec
            end
        end
    end
end

--------------------------------------------------------------------------------

ShowInfo()
EventLoop()
