local file=assert(arg[1]);local M=assert(dofile(file));local assertions=0
local function eq(a,b) assert(a==b,tostring(a)..' ~= '..tostring(b));assertions=assertions+1 end
for _,fps in ipairs({24,25,30,48,50,60,120}) do
 for _,n in ipairs({0,1,fps-1,fps,90000,125123,200000}) do eq(M.parseTimecode(M.timecode(n,fps,':'),fps),n) end
end
for _,fps in ipairs({30,60}) do
 for _,n in ipairs({0,1797,1798,1799,1800,1801,17981,17982,107892,215784,1000000}) do eq(M.parseTimecode(M.timecode(n,fps,';'),fps),n) end
end
local frame=90018;local page='color';local start,finish,left=90000,90025,0
local item={GetUniqueId=function()return 'item' end,GetStart=function()return start end,GetEnd=function()return finish end,GetLeftOffset=function()return left end,GetRightOffset=function()return 0 end}
local timeline={GetUniqueId=function()return 'timeline' end,GetCurrentVideoItem=function()return item end,GetSetting=function()return '25' end,GetCurrentTimecode=function()return M.timecode(frame,25,':') end,SetCurrentTimecode=function(_,tc)frame=M.parseTimecode(tc,25);return true end}
local project={GetUniqueId=function()return 'project' end,IsRenderingInProgress=function()return false end,GetCurrentTimeline=function()return timeline end}
local fusion={}
local resolve={GetCurrentPage=function()return page end,GetProjectManager=function()return {GetCurrentProject=function()return project end} end,Fusion=function()return fusion end}
eq(M.seek(resolve,'owner',18,12,0,24),90012)
-- A second request can arrive before the viewer redraws after the first one.
eq(M.seek(resolve,'owner',18,4,0,24),90004)
page='edit';frame=90012
eq(M.seek(resolve,'owner',112,120,100,124),90020)
-- A trimmed clip can keep source coordinates or normalize them to the trim.
page='color';frame=90012;start=90007;finish=90025;left=7
eq(M.seek(resolve,'owner',12,9,0,24),90009)
frame=90012;left=8
eq(M.seek(resolve,'owner',5,9,0,17),90016)
local before=frame;assert(not pcall(M.seek,resolve,'owner',5,24,0,24));eq(frame,before)
assert(not pcall(M.seek,resolve,'owner',5,6,0,50))
assert(not pcall(M.seek,resolve,'owner',5,6.5,0,17))
print('paint navigation: '..assertions..' timecode/mapping checks passed')
